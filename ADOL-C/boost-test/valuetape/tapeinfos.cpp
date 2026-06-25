#define BOOST_TEST_DYN_LINK
#include "../const.h"
#include <adolc/adolc.h>
#include <adolc/valuetape/infotype.h>
#include <algorithm>
#include <array>
#include <boost/test/unit_test.hpp>
#include <cstdio>

BOOST_AUTO_TEST_SUITE(Test_TapeInfos)

using TestErrorType = ADOLCError::ErrorType;
using TestOpInfo = ADOLC::detail::OpInfo<TapeInfos, TestErrorType>;
using TestLocInfo = ADOLC::detail::LocInfo<TapeInfos, TestErrorType>;
using TestValInfo = ADOLC::detail::ValInfo<TapeInfos, TestErrorType>;
using TestTayInfo = ADOLC::detail::TayInfo<TapeInfos, TestErrorType>;

static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::OpBuffer,
                                             typename TestOpInfo::value_type>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::LocBuffer,
                                             typename TestLocInfo::value_type>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::ValBuffer,
                                             typename TestValInfo::value_type>);
static_assert(ADOLC::detail::BufferStateType<ADOLC::detail::TayBuffer,
                                             typename TestTayInfo::value_type>);

static_assert(
    ADOLC::detail::InfoTypeBase<TestOpInfo, TapeInfos, TestErrorType>);
static_assert(
    ADOLC::detail::InfoTypeBase<TestLocInfo, TapeInfos, TestErrorType>);
static_assert(
    ADOLC::detail::InfoTypeBase<TestValInfo, TapeInfos, TestErrorType>);
static_assert(
    ADOLC::detail::InfoTypeBase<TestTayInfo, TapeInfos, TestErrorType>);

static_assert(ADOLC::detail::InfoType<TestOpInfo, TapeInfos, TestErrorType>);
static_assert(ADOLC::detail::InfoType<TestLocInfo, TapeInfos, TestErrorType>);
static_assert(ADOLC::detail::InfoType<TestValInfo, TapeInfos, TestErrorType>);
static_assert(!ADOLC::detail::InfoType<TestTayInfo, TapeInfos, TestErrorType>);

BOOST_AUTO_TEST_CASE(TestConstructorTapeId) {
  TapeInfos ti(3);

  BOOST_CHECK(ti.tapeId_ == 3);
}

BOOST_AUTO_TEST_CASE(TestMoveConstructor) {
  TapeInfos tp(3);
  tp.numInds = 10;
  tp.numDeps = 11;

  tp.keepTaylors = 1;
  tp.stats[2] = 5;

  auto opBuffer = new unsigned char[10];
  tp.opBuffer_ = ADOLC::detail::OpBuffer(opBuffer, 10);
  tp.opBuffer_.openFile("test_move_constr_op.txt", "w");
  tp.opBuffer_.position(3);
  tp.opBuffer_.numOnTape(10);

  tp.num_eq_prod = 4;

  auto valBuffer = new double[123];
  tp.valBuffer_ = ADOLC::detail::ValBuffer(valBuffer, 123);
  tp.valBuffer_.openFile("test_move_constr_val.txt", "w");
  tp.valBuffer_.position(15);
  tp.valBuffer_.numOnTape(123);

  auto locBuffer = new size_t[14];
  tp.locBuffer_ = ADOLC::detail::LocBuffer(locBuffer, 14);
  tp.locBuffer_.openFile("test_move_constr_loc.txt", "w");
  tp.locBuffer_.position(3);
  tp.locBuffer_.numOnTape(13);

  auto tayBuffer = new double[14];
  tp.tayBuffer_ = ADOLC::detail::TayBuffer(tayBuffer, 14);
  tp.tayBuffer_.openFile("test_move_constr_tay.txt", "w");
  tp.tayBuffer_.position(4);
  tp.tayBuffer_.numOnTape(13);

  tp.nextBufferNumber = 4;

  tp.lastTayBlockInCore = 1;

  tp.deg_save = 1;
  tp.tay_numInds = 3;
  tp.tay_numDeps = 5;

  tp.workMode = TapeInfos::NO_MODE;

  tp.ext_diff_fct_index = 5;
  tp.nestedReverseEval = true;
  tp.numSwitches = 6;

  auto l = new double[23];
  tp.signature = l;

  //// ======= MOVE CONSTRUCTOR =========
  //======================================
  TapeInfos tp2(std::move(tp));
  BOOST_CHECK_EQUAL(tp2.tapeId_, 3);
  BOOST_CHECK_EQUAL(tp2.numInds, 10);
  BOOST_CHECK_EQUAL(tp2.numDeps, 11);
  BOOST_CHECK_EQUAL(tp2.keepTaylors, 1);
  BOOST_CHECK_EQUAL(tp2.stats[2], 5);
  BOOST_CHECK_EQUAL(tp2.opBuffer_.numOnTape(), 10);
  BOOST_CHECK_EQUAL(tp2.valBuffer_.numOnTape(), 123);
  BOOST_CHECK_EQUAL(tp2.num_eq_prod, 4);

  BOOST_CHECK_EQUAL(tp2.locBuffer_.numOnTape(), 13);
  BOOST_CHECK_EQUAL(tp2.tayBuffer_.numOnTape(), 13);
  BOOST_CHECK_EQUAL(tp2.nextBufferNumber, 4);
  BOOST_CHECK_EQUAL(tp2.lastTayBlockInCore, 1);
  BOOST_CHECK_EQUAL(tp2.deg_save, 1);
  BOOST_CHECK_EQUAL(tp2.tay_numInds, 3);
  BOOST_CHECK_EQUAL(tp2.tay_numDeps, 5);
  BOOST_CHECK_EQUAL(tp2.workMode, TapeInfos::NO_MODE);
  BOOST_CHECK_EQUAL(tp2.ext_diff_fct_index, 5);
  BOOST_CHECK_EQUAL(tp2.nestedReverseEval, true);
  BOOST_CHECK_EQUAL(tp2.numSwitches, 6);

  // === Validate: pointers moved ===
  BOOST_CHECK_EQUAL(tp2.opBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(tp2.opBuffer_.begin(), opBuffer);
  BOOST_CHECK_EQUAL(tp2.opBuffer_.position(), 3);
  BOOST_CHECK_EQUAL(tp2.valBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(tp2.valBuffer_.begin(), valBuffer);
  BOOST_CHECK_EQUAL(tp2.valBuffer_.position(), 15);

  BOOST_CHECK_EQUAL(tp2.locBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(tp2.locBuffer_.begin(), locBuffer);
  BOOST_CHECK_EQUAL(tp2.locBuffer_.position(), 3);

  BOOST_CHECK_EQUAL(tp2.tayBuffer_.file() != nullptr, true);
  BOOST_CHECK_EQUAL(tp2.tayBuffer_.begin(), tayBuffer);
  BOOST_CHECK_EQUAL(tp2.tayBuffer_.position(), 4);
  BOOST_CHECK_EQUAL(tp2.signature, l);
}

BOOST_AUTO_TEST_CASE(TestOpInfoReverseSeekOffsetUsesRemainingTapeCount) {
  TapeInfos ti(7);
  ti.stats[TapeInfos::OP_BUFFER_SIZE] = 4;
  ti.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ti.opBuffer_.numOnTape(10);

  const long offset = TestOpInfo::reverseSeekOffset(ti, ti.stats);

  BOOST_CHECK_EQUAL(offset, 6);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferStatsForwardConsumesLoadedBlock) {
  TapeInfos ti(7);
  ti.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ti.opBuffer_.numOnTape(10);

  TestOpInfo::updateBufferStatsForward(ti, 4);

  BOOST_CHECK_EQUAL(ti.opBuffer_.numOnTape(), 6);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferStatsReverseConsumesLoadedBlock) {
  TapeInfos ti(7);
  ti.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ti.opBuffer_.numOnTape(10);

  TestOpInfo::updateBufferStatsReverse(ti, 4);

  BOOST_CHECK_EQUAL(ti.opBuffer_.numOnTape(), 6);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferPositionForwardResetsPosition) {
  TapeInfos ti(7);
  ti.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);
  ti.opBuffer_.position(3);

  TestOpInfo::updateBufferPositionForward(ti);

  BOOST_CHECK_EQUAL(ti.opBuffer_.position(), 0);
}

BOOST_AUTO_TEST_CASE(TestOpInfoUpdateBufferPositionReverseUsesBlockSize) {
  TapeInfos ti(7);
  ti.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);

  TestOpInfo::updateBufferPositionReverse(ti, 5);

  BOOST_CHECK_EQUAL(ti.opBuffer_.position(), 5);
}

BOOST_AUTO_TEST_CASE(TestOpInfoPrepareForwardPosition) {
  TapeInfos ti(7);
  ti.opBuffer_ = ADOLC::detail::OpBuffer(new unsigned char[8]{}, 8);

  TestOpInfo::prepareForwardPosition(ti, ti.stats);

  BOOST_CHECK_EQUAL(ti.opBuffer_.position(), 0);
}

BOOST_AUTO_TEST_CASE(TestLocInfoUpdateBufferPositionReverseUsesTailMarker) {
  TapeInfos ti(7);
  ti.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[6]{0, 0, 0, 0, 0, 2}, 6);

  TestLocInfo::updateBufferPositionReverse(ti, 6);

  BOOST_CHECK_EQUAL(ti.locBuffer_.position(), 4);
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

  TapeInfos ti(7);
  ti.stats[TapeInfos::LOC_BUFFER_SIZE] = 10;
  ti.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[10]{}, 10);
  ti.locBuffer_.openFile(fileName, "rb");
  std::fseek(ti.locBuffer_.file(), static_cast<long>(10 * sizeof(size_t)),
             SEEK_SET);
  ti.locBuffer_.numOnTape(40);

  TestLocInfo::prepareForwardPosition(ti, ti.stats);

  BOOST_CHECK_EQUAL(ti.locBuffer_.position(), 2);
  BOOST_CHECK_EQUAL(ti.locBuffer_.numOnTape(), 0);
  BOOST_CHECK_EQUAL(ti.locBuffer_[0], 40);
  BOOST_CHECK_EQUAL(ti.locBuffer_[1], 41);

  std::remove(fileName);
}

BOOST_AUTO_TEST_CASE(TestValInfoUpdateBufferPositionForwardAdvancesLocBuffer) {
  TapeInfos ti(7);
  ti.valBuffer_ = ADOLC::detail::ValBuffer(new double[4]{}, 4);
  ti.valBuffer_.position(3);
  ti.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[4], 4);
  ti.locBuffer_.position(1);

  TestValInfo::updateBufferPositionForward(ti);

  BOOST_CHECK_EQUAL(ti.locBuffer_.position(), 2);
  BOOST_CHECK_EQUAL(ti.valBuffer_.position(), 0);
}

BOOST_AUTO_TEST_CASE(
    TestValInfoUpdateBufferPositionReverseUsesLocBufferMarker) {
  TapeInfos ti(7);
  ti.valBuffer_ = ADOLC::detail::ValBuffer(new double[8], 8);
  ti.locBuffer_ = ADOLC::detail::LocBuffer(new size_t[4]{0, 0, 0, 3}, 4);
  ti.locBuffer_.position(4);

  TestValInfo::updateBufferPositionReverse(ti, 8);

  BOOST_CHECK_EQUAL(ti.valBuffer_.position(), 5);
  BOOST_CHECK_EQUAL(ti.locBuffer_.position(), 3);
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

  TapeInfos ti(7);
  ti.stats[TapeInfos::TAY_BUFFER_SIZE] = 2;
  ti.tayBuffer_ = ADOLC::detail::TayBuffer(new double[2]{0.0, 0.0}, 2);
  ti.tayBuffer_.openFile(fileName, "rb");
  ti.tayBuffer_.position(0);
  ti.nextBufferNumber = 1;
  ti.lastTayBlockInCore = 0;

  const double value = ti.loadNextReverse<TestTayInfo>();

  BOOST_CHECK_EQUAL(value, 4.0);
  BOOST_CHECK_EQUAL(ti.tayBuffer_.position(), 1);
  BOOST_CHECK_EQUAL(ti.nextBufferNumber, 0);
  BOOST_CHECK_EQUAL(ti.lastTayBlockInCore, 0);
  BOOST_CHECK_EQUAL(ti.tayBuffer_[0], 3.0);
  BOOST_CHECK_EQUAL(ti.tayBuffer_[1], 4.0);

  std::remove(fileName);
}

BOOST_AUTO_TEST_CASE(TestTayInfoReverseSeekOffsetUsesNextBufferNumber) {
  TapeInfos ti(7);
  ti.stats[TapeInfos::TAY_BUFFER_SIZE] = 3;
  ti.nextBufferNumber = 4;

  const long offset = TestTayInfo::reverseSeekOffset(ti, ti.stats);

  BOOST_CHECK_EQUAL(offset, 12 * static_cast<long>(sizeof(double)));
}

BOOST_AUTO_TEST_CASE(TestTayInfoUpdateBufferStatsReverseUpdatesReverseState) {
  TapeInfos ti(7);
  ti.nextBufferNumber = 4;
  ti.lastTayBlockInCore = 1;

  TestTayInfo::updateBufferStatsReverse(ti, 2);

  BOOST_CHECK_EQUAL(ti.nextBufferNumber, 3);
  BOOST_CHECK_EQUAL(ti.lastTayBlockInCore, 0);
}

BOOST_AUTO_TEST_SUITE_END()
