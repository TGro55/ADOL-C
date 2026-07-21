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
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <utility>

BOOST_AUTO_TEST_SUITE(Test_ThreadSafeValueTape)

namespace tt = boost::test_tools;

using namespace std::chrono_literals;

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

BOOST_AUTO_TEST_SUITE_END()
