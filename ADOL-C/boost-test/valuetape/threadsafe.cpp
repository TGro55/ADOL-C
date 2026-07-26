#define BOOST_TEST_DYN_LINK
#include "../const.h"
#include <adolc/adolc.h>
#include <adolc/valuetape/taperegistry.h>
#include <adolc/valuetape/valuetape.h>
#include <array>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cmath>
#include <future>
#include <latch>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <utility>

BOOST_AUTO_TEST_SUITE(Test_ThreadSafeValueTape)

namespace tt = boost::test_tools;

using namespace std::chrono_literals;

namespace {

short traceBufferOwnershipTape() {
  const short tapeId = createNewTape();
  trace_on(tapeId, 1);
  {
    adouble x;
    adouble y;
    double output = 0.0;
    x <<= 2.0;
    y = sin(x) + x * x;
    y >>= output;
  }
  trace_off();
  return tapeId;
}

void checkBufferOwnership(const ADOLC::detail::TapeEvaluationContext &evalCtx,
                          bool expectedOwner) {
  BOOST_CHECK_EQUAL(evalCtx.opBuffer_.isOwner(), expectedOwner);
  BOOST_CHECK_EQUAL(evalCtx.valBuffer_.isOwner(), expectedOwner);
  BOOST_CHECK_EQUAL(evalCtx.locBuffer_.isOwner(), expectedOwner);
  BOOST_CHECK_EQUAL(evalCtx.tayBuffer_.isOwner(), expectedOwner);
}

struct SharedBufferSnapshot {
  std::array<const void *, 4> pointers{};
  std::array<size_t, 4> positions{};
  bool allNonOwning{false};
};

struct ScalarSweepResult {
  int status{0};
  double value{0.0};
  double derivative{0.0};
};

struct HigherOrderForwardResult {
  int status{0};
  double value{0.0};
  std::array<double, 3> coefficients{};
};

struct HigherOrderReverseResult {
  int status{0};
  std::array<double, 2> coefficients{};
};

} // namespace

BOOST_AUTO_TEST_CASE(RecursiveRecordingIsRejectedAndNestedTapeIsRestored) {
  const short outerTapeId = createNewTape();
  auto outerTape = findTapePtr_(outerTapeId);

  const short innerTapeId = createNewTape();
  auto innerTape = findTapePtr_(innerTapeId);

  setCurrentTape(nullptr);
  outerTape->beginRecording();
  BOOST_CHECK_EQUAL(currentTapePtr(), outerTape);
  BOOST_CHECK_THROW(outerTape->beginRecording(), std::runtime_error);
  BOOST_CHECK(outerTape->isRecording());

  innerTape->beginRecording();
  BOOST_CHECK_EQUAL(currentTapePtr(), innerTape);
  BOOST_CHECK(innerTape->isRecording());
  BOOST_CHECK(!outerTape->isRecording());
  innerTape->endRecording();
  BOOST_CHECK(!innerTape->isRecording());
  BOOST_CHECK_EQUAL(currentTapePtr(), outerTape);
  outerTape->endRecording();
  BOOST_CHECK_EQUAL(currentTapePtr(), nullptr);
}

BOOST_AUTO_TEST_CASE(ParallelRecordingOfTheSameTapeIsExclusive) {
  const short tapeId = createNewTape();
  auto &tape = findTape(tapeId);
  tape.beginRecording();

  std::promise<void> attemptingRecording;
  bool secondRecorderEntered{false};
  auto secondRecorder = std::async(std::launch::async, [&] {
    attemptingRecording.set_value();
    tape.beginRecording();
    secondRecorderEntered = true;
    tape.endRecording();
  });

  attemptingRecording.get_future().wait();
  BOOST_CHECK(secondRecorder.wait_for(20ms) == std::future_status::timeout);
  BOOST_CHECK(!secondRecorderEntered);

  tape.endRecording();
  BOOST_CHECK(secondRecorder.wait_for(20ms) == std::future_status::ready);
  BOOST_CHECK_NO_THROW(secondRecorder.get());
  BOOST_CHECK(secondRecorderEntered);
}

BOOST_AUTO_TEST_CASE(TapeIdsAreGloballyUniqueAcrossThreads) {
  std::latch creatorsReady(2);
  auto createTape = [&] {
    creatorsReady.count_down();
    creatorsReady.wait();
    return createNewTape();
  };

  auto first = std::async(std::launch::async, createTape);
  auto second = std::async(std::launch::async, createTape);

  BOOST_CHECK_NE(first.get(), second.get());
}

BOOST_AUTO_TEST_CASE(ReadingIsExcludedWhileTapeIsBeingWritten) {
  const short tapeId = createNewTape();
  ValueTape &tape = findTape(tapeId);
  auto try_read = [&]() {
    return std::async(std::launch::async, [&] {
      std::shared_lock lock(tape.accessMutex(), std::try_to_lock);
      return lock.owns_lock();
    });
  };
  trace_on(tapeId);
  {
    adouble x;
    adouble y;
    x <<= 2.0;
    y = x + 1.0;

    BOOST_CHECK(!try_read().get());

    double result = 0.0;
    y >>= result;
  }
  trace_off();
  BOOST_CHECK(try_read().get());
}

BOOST_AUTO_TEST_CASE(InitSweepOwnershipMatchesAccessModeAndKeep) {
  const short tapeId = traceBufferOwnershipTape();
  auto &tape = findTape(tapeId);

  {
    auto evalCtx = tape.init_sweep<ValueTape::Forward>();
    checkBufferOwnership(evalCtx, true);
    tape.end_sweep(std::move(evalCtx));
  }
  {
    auto evalCtx = tape.init_sweep<ValueTape::Reverse>();
    checkBufferOwnership(evalCtx, true);
    tape.end_sweep(std::move(evalCtx));
  }

  tape.setSharedMode();
  {
    auto evalCtx = tape.init_sweep<ValueTape::Forward>();
    checkBufferOwnership(evalCtx, false);
    tape.end_sweep(evalCtx);
  }
  {
    auto evalCtx = tape.init_sweep<ValueTape::Reverse>();
    checkBufferOwnership(evalCtx, false);
    tape.end_sweep(evalCtx);
  }

  // A keep sweep is exclusive even while the tape is configured for shared
  // no-keep evaluation, because its results must be published to recordCtx_.
  {
    auto evalCtx = tape.init_sweep<ValueTape::Forward>(1);
    checkBufferOwnership(evalCtx, true);
    tape.end_sweep(std::move(evalCtx));
  }
}

BOOST_AUTO_TEST_CASE(ConcurrentSharedSweepsViewTheSameBufferData) {
  const short tapeId = traceBufferOwnershipTape();
  auto &tape = findTape(tapeId);
  tape.setSharedMode();

  std::latch contextsReady(2);
  std::promise<void> releaseContexts;
  auto releaseFuture = releaseContexts.get_future().share();

  auto captureSharedBuffers = [&]() {
    auto evalCtx = tape.init_sweep<ValueTape::Forward>();
    SharedBufferSnapshot snapshot{
        .pointers = {evalCtx.opBuffer_.begin(), evalCtx.valBuffer_.begin(),
                     evalCtx.locBuffer_.begin(), evalCtx.tayBuffer_.begin()},
        .positions = {evalCtx.opBuffer_.position(),
                      evalCtx.valBuffer_.position(),
                      evalCtx.locBuffer_.position(),
                      evalCtx.tayBuffer_.position()},
        .allNonOwning =
            !evalCtx.opBuffer_.isOwner() && !evalCtx.valBuffer_.isOwner() &&
            !evalCtx.locBuffer_.isOwner() && !evalCtx.tayBuffer_.isOwner()};

    contextsReady.count_down();
    contextsReady.wait();
    releaseFuture.wait();
    tape.end_sweep(evalCtx);
    return snapshot;
  };

  auto firstFuture = std::async(std::launch::async, captureSharedBuffers);
  auto secondFuture = std::async(std::launch::async, captureSharedBuffers);
  contextsReady.wait();
  releaseContexts.set_value();

  const auto first = firstFuture.get();
  const auto second = secondFuture.get();
  BOOST_CHECK(first.allNonOwning);
  BOOST_CHECK(second.allNonOwning);
  BOOST_CHECK_EQUAL_COLLECTIONS(first.pointers.begin(), first.pointers.end(),
                                second.pointers.begin(), second.pointers.end());
  BOOST_CHECK_EQUAL_COLLECTIONS(first.positions.begin(), first.positions.end(),
                                second.positions.begin(),
                                second.positions.end());
}

BOOST_AUTO_TEST_CASE(OneJacobianCanBeComputedWithParallelScalarSweeps) {
  constexpr size_t n = 3;
  constexpr size_t m = 2;
  const short tapeId = createNewTape();
  trace_on(tapeId);
  {
    std::array<adouble, n> x;
    std::array<double, n> point{};
    x <<= point;
    std::array<adouble, m> y{x[0] * x[0] + x[1] * x[2],
                             sin(x[0]) + x[1] * x[1] - x[2]};
    std::array<double, m> out{};
    y >>= out;
  }
  trace_off();
  findTape(tapeId).setSharedMode();

  const std::array<double, n> point{2.0, 3.0, 4.0};
  const std::array<std::array<double, n>, m> expected{
      std::array<double, n>{4.0, 4.0, 3.0},
      std::array<double, n>{std::cos(2.0), 6.0, -1.0}};

  std::latch forwardEvalReady(n);
  auto forwardColumn = [&, tapeId](int column) {
    std::array<double, n> direction{};
    std::array<double, m> value{};
    std::array<double, m> derivative{};
    direction[column] = 1.0;

    // ensure everything starts at same time
    forwardEvalReady.count_down();
    forwardEvalReady.wait();
    const int result =
        fos_forward(tapeId, m, n, 0, point.data(), direction.data(),
                    value.data(), derivative.data());
    return std::pair{result, derivative};
  };

  std::array<std::future<std::pair<int, std::array<double, m>>>, n>
      forwardEvals;
  for (int column = 0; column < n; ++column) {
    forwardEvals[column] =
        std::async(std::launch::async, forwardColumn, column);
  }
  for (int column = 0; column < n; ++column) {
    const auto [result, derivative] = forwardEvals[column].get();
    BOOST_CHECK_GE(result, 0);
    for (int row = 0; row < m; ++row)
      BOOST_TEST(derivative[row] == expected[row][column], tt::tolerance(tol));
  }

  std::array<double, m> value{};
  BOOST_REQUIRE_GE(zos_forward(tapeId, m, n, 1, point.data(), value.data()), 0);

  std::latch reverseEvalReady(m);
  auto reverseRow = [&, tapeId](int row) {
    std::array<double, m> weight{};
    std::array<double, n> gradient{};
    weight[row] = 1.0;

    // ensure everything starts at same time
    reverseEvalReady.count_down();
    reverseEvalReady.wait();
    const int result =
        fos_reverse(tapeId, m, n, weight.data(), gradient.data());
    return std::pair{result, gradient};
  };

  std::array<std::future<std::pair<int, std::array<double, n>>>, m>
      reverseEvals;
  for (int row = 0; row < m; ++row)
    reverseEvals[row] = std::async(std::launch::async, reverseRow, row);

  for (int row = 0; row < m; ++row) {
    const auto [result, gradient] = reverseEvals[row].get();
    BOOST_CHECK_GE(result, 0);
    for (int column = 0; column < n; ++column)
      BOOST_TEST(gradient[column] == expected[row][column], tt::tolerance(tol));
  }
}

BOOST_AUTO_TEST_CASE(OneJacobianCanBeComputedWithStdThreads) {
  constexpr int n = 3;
  constexpr int m = 2;
  const short tapeId = createNewTape();
  trace_on(tapeId);
  {
    std::array<adouble, n> x;
    std::array<double, n> point{};
    x <<= point;
    std::array<adouble, m> y{x[0] * x[0] + x[1] * x[2],
                             sin(x[0]) + x[1] * x[1] - x[2]};

    std::array<double, m> out{};
    y >>= out;
  }
  trace_off();
  findTape(tapeId).setSharedMode();

  const std::array<double, n> point{2.0, 3.0, 4.0};
  const std::array<std::array<double, n>, m> expected{
      std::array<double, n>{4.0, 4.0, 3.0},
      std::array<double, n>{std::cos(2.0), 6.0, -1.0}};

  std::array<std::array<double, m>, n> forwardColumns{};
  std::array<int, n> forwardResults{};
  std::latch forwardThreadsReady(n);
  auto forwardEval = [&](int column) {
    std::array<double, n> direction{};
    std::array<double, m> value{};
    direction[column] = 1.0;

    // ensure everything starts at same time
    forwardThreadsReady.count_down();
    forwardThreadsReady.wait();
    forwardResults[column] =
        fos_forward(tapeId, m, n, 0, point.data(), direction.data(),
                    value.data(), forwardColumns[column].data());
  };

  std::array<std::thread, n> forwardThreads;
  for (int column = 0; column < n; ++column)
    forwardThreads[column] = std::thread(forwardEval, column);
  for (auto &thread : forwardThreads)
    thread.join();

  for (int column = 0; column < n; ++column) {
    BOOST_CHECK_GE(forwardResults[column], 0);
    for (int row = 0; row < m; ++row)
      BOOST_TEST(forwardColumns[column][row] == expected[row][column],
                 tt::tolerance(tol));
  }

  std::array<double, m> value{};
  BOOST_REQUIRE_GE(zos_forward(tapeId, m, n, 1, point.data(), value.data()), 0);

  std::array<std::array<double, n>, m> reverseRows{};
  std::array<int, m> reverseResults{};
  std::latch reverseThreadsReady(m);
  auto reverseEval = [&](int row) {
    std::array<double, m> weight{};
    weight[row] = 1.0;

    // ensure everything starts at same time
    reverseThreadsReady.count_down();
    reverseThreadsReady.wait();
    reverseResults[row] =
        fos_reverse(tapeId, m, n, weight.data(), reverseRows[row].data());
  };

  std::array<std::thread, m> reverseThreads;
  for (int row = 0; row < m; ++row)
    reverseThreads[row] = std::thread(reverseEval, row);
  for (auto &thread : reverseThreads)
    thread.join();

  for (int row = 0; row < m; ++row) {
    BOOST_CHECK_GE(reverseResults[row], 0);
    for (int column = 0; column < n; ++column)
      BOOST_TEST(reverseRows[row][column] == expected[row][column],
                 tt::tolerance(tol));
  }
}

BOOST_AUTO_TEST_CASE(ConcurrentFileBackedForwardAndReverseSweeps) {
  constexpr int numWorkers = 4;
  constexpr int chainLength = 128;
  const short tapeId = createNewTape();
  ValueTape &tape = findTape(tapeId);
  tape.operationBufferSize(64);
  tape.locationBufferSize(64);
  tape.valueBufferSize(64);
  tape.taylorBufferSize(64);

  trace_on(tapeId, 1);
  {
    adouble x;
    adouble y;
    double output = 0.0;
    x <<= 0.2;
    y = x;
    for (int i = 0; i < chainLength; ++i)
      y = sin(y) + 0.001 * y * y + static_cast<double>(i + 1) * 1e-6;
    y >>= output;
  }
  trace_off(1);

  const auto stats = tapestats(tapeId);
  BOOST_REQUIRE_EQUAL(stats[TapeInfos::OP_FILE_ACCESS], size_t{1});
  BOOST_REQUIRE_EQUAL(stats[TapeInfos::LOC_FILE_ACCESS], size_t{1});
  BOOST_REQUIRE_EQUAL(stats[TapeInfos::VAL_FILE_ACCESS], size_t{1});

  // trace_on(..., keep = 1) publishes a file-backed Taylor stack. A shared
  // reverse opens an independent read stream and therefore also verifies that
  // the recording stream was flushed before publication.
  tape.setSharedMode();
  const std::array<double, 1> recordedWeight{1.0};
  std::array<double, 1> recordedDerivative{};
  BOOST_REQUIRE_GE(fos_reverse(tapeId, 1, 1, recordedWeight.data(),
                               recordedDerivative.data()),
                   0);
  tape.setExclusiveMode();

  const std::array<double, 1> point{0.3};
  const std::array<double, 1> direction{1.0};
  std::array<double, 1> forwardValue{};
  std::array<double, 1> forwardDerivative{};
  BOOST_REQUIRE_GE(fos_forward(tapeId, 1, 1, 0, point.data(), direction.data(),
                               forwardValue.data(), forwardDerivative.data()),
                   0);

  std::array<double, 1> primal{};
  BOOST_REQUIRE_GE(zos_forward(tapeId, 1, 1, 1, point.data(), primal.data()),
                   0);
  const std::array<double, 1> weight{1.0};
  std::array<double, 1> reverseDerivative{};
  BOOST_REQUIRE_GE(
      fos_reverse(tapeId, 1, 1, weight.data(), reverseDerivative.data()), 0);

  BOOST_REQUIRE_GE(zos_forward(tapeId, 1, 1, 1, point.data(), primal.data()),
                   0);
  tape.setSharedMode();

  std::latch forwardWorkersReady(numWorkers);
  auto runForward = [&] {
    ScalarSweepResult result;
    const auto localPoint = point;
    const auto localDirection = direction;
    std::array<double, 1> value{};
    std::array<double, 1> derivative{};
    forwardWorkersReady.count_down();
    forwardWorkersReady.wait();
    result.status =
        fos_forward(tapeId, 1, 1, 0, localPoint.data(), localDirection.data(),
                    value.data(), derivative.data());
    result.value = value[0];
    result.derivative = derivative[0];
    return result;
  };

  std::array<std::future<ScalarSweepResult>, numWorkers> forwardSweeps;
  for (int worker = 0; worker < numWorkers; ++worker)
    forwardSweeps[worker] = std::async(std::launch::async, runForward);

  for (auto &sweep : forwardSweeps) {
    const auto result = sweep.get();
    BOOST_CHECK_GE(result.status, 0);
    BOOST_TEST(result.value == forwardValue[0], tt::tolerance(tol));
    BOOST_TEST(result.derivative == forwardDerivative[0], tt::tolerance(tol));
  }

  std::latch reverseWorkersReady(numWorkers);
  auto runReverse = [&] {
    ScalarSweepResult result;
    const auto localWeight = weight;
    std::array<double, 1> derivative{};
    reverseWorkersReady.count_down();
    reverseWorkersReady.wait();
    result.status =
        fos_reverse(tapeId, 1, 1, localWeight.data(), derivative.data());
    result.derivative = derivative[0];
    return result;
  };

  std::array<std::future<ScalarSweepResult>, numWorkers> reverseSweeps;
  for (int worker = 0; worker < numWorkers; ++worker)
    reverseSweeps[worker] = std::async(std::launch::async, runReverse);

  for (auto &sweep : reverseSweeps) {
    const auto result = sweep.get();
    BOOST_CHECK_GE(result.status, 0);
    BOOST_TEST(result.derivative == reverseDerivative[0], tt::tolerance(tol));
  }
}

BOOST_AUTO_TEST_CASE(ConcurrentHigherOrderForwardAndReverseSweeps) {
  constexpr int numWorkers = 4;
  constexpr int degree = 3;
  const short tapeId = createNewTape();
  trace_on(tapeId, 1);
  {
    adouble x;
    adouble y;
    double output = 0.0;
    x <<= 1.25;
    y = sin(x) + x * x * x;
    y >>= output;
  }
  trace_off();

  const std::array<double, 1> point{0.75};
  const std::array<double, degree> inputCoefficients{1.0, 0.25, -0.125};
  const double *inputRows[]{inputCoefficients.data()};
  std::array<double, 1> forwardValue{};
  std::array<double, degree> forwardCoefficients{};
  double *outputRows[]{forwardCoefficients.data()};
  BOOST_REQUIRE_GE(hos_forward(tapeId, 1, 1, degree, 0, point.data(), inputRows,
                               forwardValue.data(), outputRows),
                   0);

  const std::array<double, 1> firstDirection{1.0};
  std::array<double, 1> firstOrderValue{};
  std::array<double, 1> firstOrderDerivative{};
  BOOST_REQUIRE_GE(fos_forward(tapeId, 1, 1, 2, point.data(),
                               firstDirection.data(), firstOrderValue.data(),
                               firstOrderDerivative.data()),
                   0);
  const std::array<double, 1> weight{1.0};
  std::array<double, 2> reverseCoefficients{};
  double *reverseRows[]{reverseCoefficients.data()};
  BOOST_REQUIRE_GE(hos_reverse(tapeId, 1, 1, 1, weight.data(), reverseRows), 0);

  BOOST_REQUIRE_GE(fos_forward(tapeId, 1, 1, 2, point.data(),
                               firstDirection.data(), firstOrderValue.data(),
                               firstOrderDerivative.data()),
                   0);
  findTape(tapeId).setSharedMode();

  std::latch workersReady(2 * numWorkers);
  auto runForward = [&] {
    HigherOrderForwardResult result;
    const auto localPoint = point;
    const auto localInputCoefficients = inputCoefficients;
    const double *localInputRows[]{localInputCoefficients.data()};
    std::array<double, 1> value{};
    double *localOutputRows[]{result.coefficients.data()};
    workersReady.count_down();
    workersReady.wait();
    result.status = hos_forward(tapeId, 1, 1, degree, 0, localPoint.data(),
                                localInputRows, value.data(), localOutputRows);
    result.value = value[0];
    return result;
  };
  auto runReverse = [&] {
    HigherOrderReverseResult result;
    const auto localWeight = weight;
    double *localReverseRows[]{result.coefficients.data()};
    workersReady.count_down();
    workersReady.wait();
    result.status =
        hos_reverse(tapeId, 1, 1, 1, localWeight.data(), localReverseRows);
    return result;
  };

  std::array<std::future<HigherOrderForwardResult>, numWorkers> forwardSweeps;
  std::array<std::future<HigherOrderReverseResult>, numWorkers> reverseSweeps;
  for (int worker = 0; worker < numWorkers; ++worker) {
    forwardSweeps[worker] = std::async(std::launch::async, runForward);
    reverseSweeps[worker] = std::async(std::launch::async, runReverse);
  }

  for (auto &sweep : forwardSweeps) {
    const auto result = sweep.get();
    BOOST_CHECK_GE(result.status, 0);
    BOOST_TEST(result.value == forwardValue[0], tt::tolerance(tol));
    for (int order = 0; order < degree; ++order)
      BOOST_TEST(result.coefficients[order] == forwardCoefficients[order],
                 tt::tolerance(tol));
  }
  for (auto &sweep : reverseSweeps) {
    const auto result = sweep.get();
    BOOST_CHECK_GE(result.status, 0);
    for (int order = 0; order < 2; ++order)
      BOOST_TEST(result.coefficients[order] == reverseCoefficients[order],
                 tt::tolerance(tol));
  }
}

BOOST_AUTO_TEST_CASE(ModeChangeWaitsForMovedEvaluationContext) {
  const short tapeId = createNewTape();
  ValueTape &tape = findTape(tapeId);
  trace_on(tapeId);
  {
    adouble x;
    adouble y;
    double output = 0.0;
    x <<= 2.0;
    y = x * x;
    y >>= output;
  }
  trace_off();

  // emplace forces the evaluation context's move constructor to transfer both
  // the tape-data lease and the shared mode lease.
  std::optional<ADOLC::detail::TapeEvaluationContext> evalCtx;
  evalCtx.emplace(tape.init_sweep<ValueTape::Forward>());

  std::promise<void> attemptingModeChange;
  auto modeChange = std::async(std::launch::async, [&] {
    attemptingModeChange.set_value();
    tape.setSharedMode();
  });

  attemptingModeChange.get_future().wait();
  BOOST_CHECK(modeChange.wait_for(20ms) == std::future_status::timeout);
  BOOST_CHECK(tape.isExclusiveNonLocking());

  tape.end_sweep(std::move(*evalCtx));
  evalCtx.reset();
  BOOST_REQUIRE(modeChange.wait_for(1s) == std::future_status::ready);
  BOOST_CHECK_NO_THROW(modeChange.get());
  BOOST_CHECK(!tape.isExclusiveLocking());

  tape.setExclusiveMode();
  BOOST_CHECK(tape.isExclusiveLocking());
}

BOOST_AUTO_TEST_CASE(ModeChangeIsRejectedWhileRecording) {
  const short tapeId = createNewTape();
  ValueTape &tape = findTape(tapeId);

  trace_on(tapeId);
  BOOST_CHECK_THROW(tape.setSharedMode(), ADOLCError::ADOLCError);
  trace_off();
  BOOST_CHECK(tape.isExclusiveLocking());
}

BOOST_AUTO_TEST_SUITE_END()
