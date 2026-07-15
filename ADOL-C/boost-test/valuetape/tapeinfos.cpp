#define BOOST_TEST_DYN_LINK
#include "../const.h"
#include <adolc/adolc.h>
#include <algorithm>
#include <array>
#include <boost/test/unit_test.hpp>
#include <cstdio>

BOOST_AUTO_TEST_SUITE(Test_TapeInfos)

using TestErrorType = ADOLCError::ErrorType;
using TestRecordingContext = ADOLC::detail::TapeRecordingContext;
using TestEvaluationContext = ADOLC::detail::TapeEvaluationContext;
using TestOpInfo = ADOLC::detail::OpInfo<TestRecordingContext, TestErrorType>;
using TestLocInfo = ADOLC::detail::LocInfo<TestRecordingContext, TestErrorType>;
using TestValInfo = ADOLC::detail::ValInfo<TestRecordingContext, TestErrorType>;
using TestTayInfo = ADOLC::detail::TayInfo<TestRecordingContext, TestErrorType>;
using TestEvalLocInfo =
    ADOLC::detail::LocInfo<TestEvaluationContext, TestErrorType>;
using TestEvalOpInfo =
    ADOLC::detail::OpInfo<TestEvaluationContext, TestErrorType>;
using TestEvalTayInfo =
    ADOLC::detail::TayInfo<TestEvaluationContext, TestErrorType>;
using StatsArray = std::array<size_t, TapeInfos::STAT_SIZE>;

static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::OpBuffer,
                                             typename TestOpInfo::value_type>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::LocBuffer,
                                             typename TestLocInfo::value_type>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::ValBuffer,
                                             typename TestValInfo::value_type>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::TayBuffer,
                                             typename TestTayInfo::value_type>);

static_assert(
    ADOLC::detail::InfoType<TestOpInfo, TestRecordingContext, TestErrorType>);
static_assert(
    ADOLC::detail::InfoType<TestLocInfo, TestRecordingContext, TestErrorType>);
static_assert(
    ADOLC::detail::InfoType<TestValInfo, TestRecordingContext, TestErrorType>);
static_assert(
    ADOLC::detail::InfoType<TestTayInfo, TestRecordingContext, TestErrorType>);

BOOST_AUTO_TEST_CASE(TestConstructorTapeId) {
  TapeInfos ti(3);

  BOOST_CHECK(ti.tapeId_ == 3);
}

BOOST_AUTO_TEST_CASE(TestTapeInfosMoveKeepsMetadataOnly) {
  TapeInfos tp(3);
  tp.stats[2] = 5;

  TapeInfos tp2(std::move(tp));

  BOOST_CHECK_EQUAL(tp2.tapeId_, 3);
  BOOST_CHECK_EQUAL(tp2.stats[2], 5);
}

BOOST_AUTO_TEST_CASE(TestRecordingContextMoveTransfersBuffersAndSignature) {
  TestRecordingContext ctx;
  ctx.numInds = 10;
  ctx.numDeps = 11;
  ctx.keepTaylors = 1;

  auto opBuffer = new unsigned char[10];
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(opBuffer, 10);
  ctx.opBuffer_.openFile("test_move_constr_op.txt", "w");
  ctx.opBuffer_.position(3);
  ctx.opBuffer_.numOnTape(10);

  ctx.num_eq_prod = 4;

  auto valBuffer = new double[123];
  ctx.valBuffer_ = ADOLC::detail::ValBuffer(valBuffer, 123);
  ctx.valBuffer_.openFile("test_move_constr_val.txt", "w");
  ctx.valBuffer_.position(15);
  ctx.valBuffer_.numOnTape(123);

  auto locBuffer = new size_t[14];
  ctx.locBuffer_ = ADOLC::detail::LocBuffer(locBuffer, 14);
  ctx.locBuffer_.openFile("test_move_constr_loc.txt", "w");
  ctx.locBuffer_.position(3);
  ctx.locBuffer_.numOnTape(13);

  auto tayBuffer = new double[14];
  ctx.tayBuffer_ = ADOLC::detail::TayBuffer(tayBuffer, 14);
  ctx.tayBuffer_.openFile("test_move_constr_tay.txt", "w");
  ctx.tayBuffer_.position(4);
  ctx.tayBuffer_.numOnTape(13);

  ctx.nextBufferNumber = 4;
  ctx.lastTayBlockInCore = 1;
  ctx.deg_save = 1;
  ctx.tay_numInds = 3;
  ctx.tay_numDeps = 5;
  ctx.nestedReverseEval = true;
  ctx.numSwitches = 6;

  auto signature = new double[23];
  ctx.signature = signature;

  TestRecordingContext ctx2(std::move(ctx));

  BOOST_CHECK_EQUAL(ctx2.numInds, 10);
  BOOST_CHECK_EQUAL(ctx2.numDeps, 11);
  BOOST_CHECK_EQUAL(ctx2.keepTaylors, 1);
  BOOST_CHECK_EQUAL(ctx2.opBuffer_.numOnTape(), 10);
  BOOST_CHECK_EQUAL(ctx2.valBuffer_.numOnTape(), 123);
  BOOST_CHECK_EQUAL(ctx2.num_eq_prod, 4);
  BOOST_CHECK_EQUAL(ctx2.locBuffer_.numOnTape(), 13);
  BOOST_CHECK_EQUAL(ctx2.tayBuffer_.numOnTape(), 13);
  BOOST_CHECK_EQUAL(ctx2.nextBufferNumber, 4);
  BOOST_CHECK_EQUAL(ctx2.lastTayBlockInCore, 1);
  BOOST_CHECK_EQUAL(ctx2.deg_save, 1);
  BOOST_CHECK_EQUAL(ctx2.tay_numInds, 3);
  BOOST_CHECK_EQUAL(ctx2.tay_numDeps, 5);
  BOOST_CHECK_EQUAL(ctx2.nestedReverseEval, true);
  BOOST_CHECK_EQUAL(ctx2.numSwitches, 6);

  BOOST_CHECK_EQUAL(ctx2.opBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(ctx2.opBuffer_.begin(), opBuffer);
  BOOST_CHECK_EQUAL(ctx2.opBuffer_.position(), 3);
  BOOST_CHECK_EQUAL(ctx2.valBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(ctx2.valBuffer_.begin(), valBuffer);
  BOOST_CHECK_EQUAL(ctx2.valBuffer_.position(), 15);
  BOOST_CHECK_EQUAL(ctx2.locBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(ctx2.locBuffer_.begin(), locBuffer);
  BOOST_CHECK_EQUAL(ctx2.locBuffer_.position(), 3);
  BOOST_CHECK_EQUAL(ctx2.tayBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(ctx2.tayBuffer_.begin(), tayBuffer);
  BOOST_CHECK_EQUAL(ctx2.tayBuffer_.position(), 4);
  BOOST_CHECK_EQUAL(ctx2.signature, signature);
  BOOST_CHECK_EQUAL(ctx.signature, nullptr);
}

BOOST_AUTO_TEST_CASE(TestOpInfoReverseSeekOffsetUsesRemainingTapeCount) {
  TestRecordingContext ctx;
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ctx.opBuffer_.numOnTape(10);

  const long offset = TestOpInfo::reverseSeekOffset(ctx, 4);

  BOOST_CHECK_EQUAL(offset, 6);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferStatsForwardConsumesLoadedBlock) {
  TestRecordingContext ctx;
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ctx.opBuffer_.numOnTape(10);

  TestOpInfo::updateBufferStatsForward(ctx, 4);

  BOOST_CHECK_EQUAL(ctx.opBuffer_.numOnTape(), 6);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferStatsReverseConsumesLoadedBlock) {
  TestRecordingContext ctx;
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ctx.opBuffer_.numOnTape(10);

  TestOpInfo::updateBufferStatsReverse(ctx, 4);

  BOOST_CHECK_EQUAL(ctx.opBuffer_.numOnTape(), 6);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferPositionForwardResetsPosition) {
  TestRecordingContext ctx;
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ctx.opBuffer_.position(3);

  TestOpInfo::updateBufferPositionForward(ctx);

  BOOST_CHECK_EQUAL(ctx.opBuffer_.position(), 0);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferPositionReverseUsesBlockSize) {
  TestRecordingContext ctx;
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);

  TestOpInfo::updateBufferPositionReverse(ctx, 5);

  BOOST_CHECK_EQUAL(ctx.opBuffer_.position(), 5);
}

BOOST_AUTO_TEST_CASE(TestOpInfoPrepareForwardPosition) {
  TestRecordingContext recordCtx;
  TestEvaluationContext ctx(recordCtx);
  StatsArray stats{};
  ctx.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);

  TestEvalOpInfo::prepareForwardPosition(ctx,
                                         stats[TestEvalOpInfo::bufferSize]);

  BOOST_CHECK_EQUAL(ctx.opBuffer_.position(), 0);
}

BOOST_AUTO_TEST_CASE(TestLocInfoUpdateBufferPositionReverseUsesTailMarker) {
  TestRecordingContext ctx;
  ctx.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[6]{0, 0, 0, 0, 0, 2}, 6);

  TestLocInfo::updateBufferPositionReverse(ctx, 6);

  BOOST_CHECK_EQUAL(ctx.locBuffer_.position(), 4);
}

BOOST_AUTO_TEST_CASE(TestLocInfoPrepareForwardPositionHonorsStatSpace) {
  constexpr char fileName[] = "test_loc_prepare_forward.bin";
  std::remove(fileName);

  std::array<size_t, 50> tapeData{};
  for (size_t i = 0; i < tapeData.size(); ++i) {
    tapeData[i] = i;
  }

  FILE *file = std::fopen(fileName, "wb");
  BOOST_REQUIRE_NE(file, nullptr);
  BOOST_REQUIRE_EQUAL(
      std::fwrite(tapeData.data(), sizeof(size_t), tapeData.size(), file),
      tapeData.size());
  std::fclose(file);

  TestRecordingContext recordCtx;
  TestEvaluationContext ctx(recordCtx);
  StatsArray stats{};
  stats[TapeInfos::LOC_BUFFER_SIZE] = 10;
  ctx.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[10]{}, 10);
  ctx.locBuffer_.openFile(fileName, "rb");
  std::fseek(ctx.locBuffer_.file(), static_cast<long>(10 * sizeof(size_t)),
             SEEK_SET);
  ctx.locBuffer_.numOnTape(40);

  TestEvalLocInfo::prepareForwardPosition(ctx,
                                          stats[TestEvalLocInfo::bufferSize]);

  BOOST_CHECK_EQUAL(ctx.locBuffer_.position(), 2);
  BOOST_CHECK_EQUAL(ctx.locBuffer_.numOnTape(), 0);
  BOOST_CHECK_EQUAL(ctx.locBuffer_[0], 40);
  BOOST_CHECK_EQUAL(ctx.locBuffer_[1], 41);

  std::remove(fileName);
}

BOOST_AUTO_TEST_CASE(TestValInfoUpdateBufferPositionForwardAdvancesLocBuffer) {
  TestRecordingContext ctx;
  ctx.valBuffer_ = ADOLC::detail::ValBuffer(new double[4]{}, 4);
  ctx.valBuffer_.position(3);
  ctx.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[4], 4);
  ctx.locBuffer_.position(1);

  TestValInfo::updateBufferPositionForward(ctx);

  BOOST_CHECK_EQUAL(ctx.locBuffer_.position(), 2);
  BOOST_CHECK_EQUAL(ctx.valBuffer_.position(), 0);
}

BOOST_AUTO_TEST_CASE(
    TestValInfoUpdateBufferPositionReverseUsesLocBufferMarker) {
  TestRecordingContext ctx;
  ctx.valBuffer_ = ADOLC::detail::ValBuffer(new double[8], 8);
  ctx.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[4]{0, 0, 0, 3}, 4);
  ctx.locBuffer_.position(4);

  TestValInfo::updateBufferPositionReverse(ctx, 8);

  BOOST_CHECK_EQUAL(ctx.valBuffer_.position(), 5);
  BOOST_CHECK_EQUAL(ctx.locBuffer_.position(), 3);
}

BOOST_AUTO_TEST_CASE(TestTayInfoEnsureReverseReadableLoadsBoundaryBlock) {
  constexpr char fileName[] = "test_tay_reverse_boundary.bin";
  std::remove(fileName);

  std::array<double, 4> tapeData{1.0, 2.0, 3.0, 4.0};
  FILE *file = std::fopen(fileName, "wb");
  BOOST_REQUIRE_NE(file, nullptr);
  BOOST_REQUIRE_EQUAL(
      std::fwrite(tapeData.data(), sizeof(double), tapeData.size(), file),
      tapeData.size());
  std::fclose(file);

  TestRecordingContext recordCtx;
  TestEvaluationContext ctx(recordCtx);
  StatsArray stats{};
  stats[TapeInfos::TAY_BUFFER_SIZE] = 2;
  ctx.tayBuffer_ = ADOLC::detail::TayBuffer(new double[2]{0.0, 0.0}, 2);
  ctx.tayBuffer_.openFile(fileName, "rb");
  ctx.tayBuffer_.position(0);
  ctx.nextBufferNumber = 1;
  ctx.lastTayBlockInCore = 0;

  const double value =
      ctx.loadNextReverse<TestEvalTayInfo>(stats[TestEvalTayInfo::bufferSize]);

  BOOST_CHECK_EQUAL(value, 4.0);
  BOOST_CHECK_EQUAL(ctx.tayBuffer_.position(), 1);
  BOOST_CHECK_EQUAL(ctx.nextBufferNumber, 0);
  BOOST_CHECK_EQUAL(ctx.lastTayBlockInCore, 0);
  BOOST_CHECK_EQUAL(ctx.tayBuffer_[0], 3.0);
  BOOST_CHECK_EQUAL(ctx.tayBuffer_[1], 4.0);

  std::remove(fileName);
}

BOOST_AUTO_TEST_CASE(TestTayInfoReverseSeekOffsetUsesNextBufferNumber) {
  TestRecordingContext ctx;
  ctx.nextBufferNumber = 4;

  const long offset = TestTayInfo::reverseSeekOffset(ctx, 3);

  BOOST_CHECK_EQUAL(offset, 12 * static_cast<long>(sizeof(double)));
}

BOOST_AUTO_TEST_CASE(TestTayInfoUpdateBufferStatsReverseUpdatesReverseState) {
  TestRecordingContext ctx;
  ctx.nextBufferNumber = 4;
  ctx.lastTayBlockInCore = 1;

  TestTayInfo::updateBufferStatsReverse(ctx, 2);

  BOOST_CHECK_EQUAL(ctx.nextBufferNumber, 3);
  BOOST_CHECK_EQUAL(ctx.lastTayBlockInCore, 0);
}

BOOST_AUTO_TEST_SUITE_END()
