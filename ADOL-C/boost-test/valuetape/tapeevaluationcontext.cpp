#define BOOST_TEST_DYN_LINK
#include "../const.h"
#include <adolc/adolc.h>
#include <adolc/valuetape/infotype.h>
#include <adolc/valuetape/tapeevaluationcontext.h>
#include <boost/test/unit_test.hpp>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>

BOOST_AUTO_TEST_SUITE(Test_TapeEvaluationContext)

using TestErrorType = ADOLCError::ErrorType;
using EvalContext = ADOLC::detail::TapeEvaluationContext;
using EvalOpInfo = ADOLC::detail::OpInfo<EvalContext, TestErrorType>;
using EvalLocInfo = ADOLC::detail::LocInfo<EvalContext, TestErrorType>;
using EvalValInfo = ADOLC::detail::ValInfo<EvalContext, TestErrorType>;
using EvalTayInfo = ADOLC::detail::TayInfo<EvalContext, TestErrorType>;

namespace {

std::shared_mutex testMutex;

std::shared_lock<std::shared_mutex> acquireTestReadLock() {
  return std::shared_lock<std::shared_mutex>(testMutex);
}

} // namespace

static_assert(ADOLC::detail::InfoType<EvalOpInfo, EvalContext, TestErrorType>);
static_assert(ADOLC::detail::InfoType<EvalLocInfo, EvalContext, TestErrorType>);
static_assert(ADOLC::detail::InfoType<EvalValInfo, EvalContext, TestErrorType>);
static_assert(ADOLC::detail::InfoType<EvalTayInfo, EvalContext, TestErrorType>);

BOOST_AUTO_TEST_CASE(TestEvaluationContextValueVectorHelpersUseLocalBuffer) {
  TapeRecordingContext tapeCtx;
  TapeInfos::StatArray stats{};
  EvalContext ctx(tapeCtx, stats, acquireTestReadLock());
  ctx.valBuffer_ =
      ADOLC::detail::ValBuffer(new double[5]{1.0, 2.0, 3.0, 4.0, 5.0}, 5);
  ctx.valBuffer_.position(1);

  double *forward = ctx.get_val_v_f(2);
  BOOST_REQUIRE_NE(forward, nullptr);
  BOOST_CHECK_EQUAL(forward[0], 2.0);
  BOOST_CHECK_EQUAL(forward[1], 3.0);
  BOOST_CHECK_EQUAL(ctx.valBuffer_.position(), size_t{3});

  double *reverse = ctx.get_val_v_r(2);
  BOOST_REQUIRE_NE(reverse, nullptr);
  BOOST_CHECK_EQUAL(reverse[0], 2.0);
  BOOST_CHECK_EQUAL(reverse[1], 3.0);
  BOOST_CHECK_EQUAL(ctx.valBuffer_.position(), size_t{1});
}

BOOST_AUTO_TEST_CASE(TestEvaluationContextGetTaylorsUsesLocalTaylorBuffer) {
  TapeRecordingContext tapeCtx;
  TapeInfos::StatArray stats{};
  EvalContext ctx(tapeCtx, stats, acquireTestReadLock());
  ctx.tayBuffer_ =
      ADOLC::detail::TayBuffer(new double[4]{1.0, 2.0, 3.0, 4.0}, 4);
  ctx.tayBuffer_.position(4);

  double coeffs[2]{0.0, 0.0};
  ctx.get_taylors(coeffs, 2, 4);

  BOOST_CHECK_EQUAL(coeffs[0], 3.0);
  BOOST_CHECK_EQUAL(coeffs[1], 4.0);
  BOOST_CHECK_EQUAL(ctx.tayBuffer_.position(), size_t{2});
}

BOOST_AUTO_TEST_CASE(TestEvaluationContextClonesRecordingState) {
  ADOLC::detail::TapeRecordingContext recordCtx{};
  recordCtx.paramstore = new double[2]{1.0, 2.0};
  auto buffer = new double[2]{3.0, 4.0};
  recordCtx.tayBuffer_ = ADOLC::detail::TayBuffer(buffer, 2);
  auto oldPtr = recordCtx.paramstore;

  TapeInfos::StatArray stats{};
  stats[TapeInfos::NUM_PARAM] = 2;
  EvalContext ctx(recordCtx, stats, acquireTestReadLock());
  BOOST_REQUIRE_NE(ctx.paramstore, nullptr);
  BOOST_CHECK_NE(ctx.paramstore, oldPtr);
  BOOST_CHECK_EQUAL(recordCtx.paramstore, oldPtr);
  BOOST_CHECK_EQUAL(ctx.paramstore[0], 1.0);
  BOOST_CHECK_EQUAL(ctx.paramstore[1], 2.0);
  BOOST_REQUIRE_NE(ctx.tayBuffer_.begin(), nullptr);
  BOOST_CHECK_NE(ctx.tayBuffer_.begin(), buffer);
  BOOST_CHECK_EQUAL(recordCtx.tayBuffer_.begin(), buffer);
  BOOST_CHECK_EQUAL(ctx.tayBuffer_.begin()[0], 3.0);
  BOOST_CHECK_EQUAL(ctx.tayBuffer_.begin()[1], 4.0);
}

BOOST_AUTO_TEST_CASE(TestEvaluationContextMoveRetainsReadLock) {
  TapeRecordingContext tapeCtx;
  TapeInfos::StatArray stats{};
  std::shared_mutex mutex;
  std::optional<EvalContext> movedCtx;

  {
    EvalContext ctx(tapeCtx, stats,
                    std::shared_lock<std::shared_mutex>(mutex));
    movedCtx.emplace(std::move(ctx));
  }

  {
    std::unique_lock writeLock(mutex, std::try_to_lock);
    BOOST_CHECK(!writeLock.owns_lock());
  }

  movedCtx.reset();

  std::unique_lock writeLock(mutex, std::try_to_lock);
  BOOST_CHECK(writeLock.owns_lock());
}

BOOST_AUTO_TEST_CASE(TestEvaluationContextTaylorBackUsesFullInCoreBlock) {
  TapeRecordingContext tapeCtx;
  TapeInfos::StatArray stats{};
  EvalContext ctx(tapeCtx, stats, acquireTestReadLock());
  ctx.tayBuffer_ = ADOLC::detail::TayBuffer(new double[2]{7.0, 8.0}, 2);
  ctx.tayBuffer_.numOnTape(2);
  ctx.lastTayBlockInCore = 1;

  ctx.taylor_back(2, 1, "unused");

  BOOST_CHECK_EQUAL(ctx.tayBuffer_.position(), size_t{2});
  BOOST_CHECK_EQUAL(ctx.loadNextReverse<EvalTayInfo>(2), 8.0);
  BOOST_CHECK_EQUAL(ctx.loadNextReverse<EvalTayInfo>(2), 7.0);
}

BOOST_AUTO_TEST_CASE(TestEvaluationContextTaylorBackOpensFileForPriorBlocks) {
  const auto path = std::filesystem::temp_directory_path() /
                    "adolc_evalctx_taylor_back_prior_blocks.bin";
  std::filesystem::remove(path);

  {
    std::FILE *file = std::fopen(path.string().c_str(), "wb");
    BOOST_REQUIRE_NE(file, nullptr);
    const double values[4]{1.0, 2.0, 3.0, 4.0};
    BOOST_REQUIRE_EQUAL(std::fwrite(values, sizeof(double), 4, file),
                        size_t{4});
    std::fclose(file);
  }

  TapeRecordingContext tapeCtx;
  TapeInfos::StatArray stats{};
  EvalContext ctx(tapeCtx, stats, acquireTestReadLock());
  ctx.tayBuffer_ = ADOLC::detail::TayBuffer(new double[2]{3.0, 4.0}, 2);
  ctx.tayBuffer_.numOnTape(4);
  ctx.lastTayBlockInCore = 1;

  ctx.taylor_back(2, 1, path.string().c_str());

  BOOST_CHECK_NE(ctx.tayBuffer_.file(), nullptr);
  BOOST_CHECK_EQUAL(ctx.loadNextReverse<EvalTayInfo>(2), 4.0);
  BOOST_CHECK_EQUAL(ctx.loadNextReverse<EvalTayInfo>(2), 3.0);
  BOOST_CHECK_EQUAL(ctx.loadNextReverse<EvalTayInfo>(2), 2.0);
  BOOST_CHECK_EQUAL(ctx.loadNextReverse<EvalTayInfo>(2), 1.0);

  ctx.tayBuffer_.closeFile();
  std::filesystem::remove(path);
}

BOOST_AUTO_TEST_SUITE_END()
