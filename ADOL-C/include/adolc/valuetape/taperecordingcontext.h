#ifndef ADOLC_TAPE_RECORDING_CONTEXT_H
#define ADOLC_TAPE_RECORDING_CONTEXT_H

#include <adolc/valuetape/infotype.h>
#include <adolc/valuetape/tapeinfos.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

namespace ADOLC::detail {

struct TapeRecordingContext {
  using StatEntries = TapeInfos::StatEntries;
  static constexpr size_t STAT_SIZE = TapeInfos::STAT_SIZE;

  static constexpr StatEntries NUM_INDEPENDENTS = TapeInfos::NUM_INDEPENDENTS;
  static constexpr StatEntries NUM_DEPENDENTS = TapeInfos::NUM_DEPENDENTS;
  static constexpr StatEntries NUM_MAX_LIVES = TapeInfos::NUM_MAX_LIVES;
  static constexpr StatEntries NUM_TAYS = TapeInfos::NUM_TAYS;
  static constexpr StatEntries OP_BUFFER_SIZE = TapeInfos::OP_BUFFER_SIZE;
  static constexpr StatEntries NUM_OPERATIONS = TapeInfos::NUM_OPERATIONS;
  static constexpr StatEntries OP_FILE_ACCESS = TapeInfos::OP_FILE_ACCESS;
  static constexpr StatEntries NUM_LOCATIONS = TapeInfos::NUM_LOCATIONS;
  static constexpr StatEntries LOC_FILE_ACCESS = TapeInfos::LOC_FILE_ACCESS;
  static constexpr StatEntries NUM_VALUES = TapeInfos::NUM_VALUES;
  static constexpr StatEntries VAL_FILE_ACCESS = TapeInfos::VAL_FILE_ACCESS;
  static constexpr StatEntries LOC_BUFFER_SIZE = TapeInfos::LOC_BUFFER_SIZE;
  static constexpr StatEntries VAL_BUFFER_SIZE = TapeInfos::VAL_BUFFER_SIZE;
  static constexpr StatEntries TAY_BUFFER_SIZE = TapeInfos::TAY_BUFFER_SIZE;
  static constexpr StatEntries NUM_EQ_PROD = TapeInfos::NUM_EQ_PROD;
  static constexpr StatEntries NO_MIN_MAX = TapeInfos::NO_MIN_MAX;
  static constexpr StatEntries NUM_SWITCHES = TapeInfos::NUM_SWITCHES;
  static constexpr StatEntries NUM_PARAM = TapeInfos::NUM_PARAM;
  static constexpr size_t maxLocsPerOp = TapeInfos::maxLocsPerOp;

  TapeRecordingContext() = default;
  ~TapeRecordingContext() {
    delete[] signature;
    signature = nullptr;
  }

  TapeRecordingContext(const TapeRecordingContext &) = delete;
  TapeRecordingContext &operator=(const TapeRecordingContext &) = delete;

  TapeRecordingContext(TapeRecordingContext &&other) noexcept
      : opBuffer_(std::move(other.opBuffer_)),
        valBuffer_(std::move(other.valBuffer_)),
        locBuffer_(std::move(other.locBuffer_)),
        tayBuffer_(std::move(other.tayBuffer_)), numInds(other.numInds),
        numDeps(other.numDeps), keepTaylors(other.keepTaylors),
        num_eq_prod(other.num_eq_prod), deg_save(other.deg_save),
        tay_numInds(other.tay_numInds), tay_numDeps(other.tay_numDeps),
        numSwitches(other.numSwitches),
        nestedReverseEval(other.nestedReverseEval),
        ext_diff_fct_index(other.ext_diff_fct_index),
        nextBufferNumber(other.nextBufferNumber),
        lastTayBlockInCore(other.lastTayBlockInCore),
        signature(std::exchange(other.signature, nullptr)) {}

  TapeRecordingContext &operator=(TapeRecordingContext &&other) noexcept {
    if (this != &other) {
      opBuffer_ = std::move(other.opBuffer_);
      valBuffer_ = std::move(other.valBuffer_);
      locBuffer_ = std::move(other.locBuffer_);
      tayBuffer_ = std::move(other.tayBuffer_);

      numInds = other.numInds;
      numDeps = other.numDeps;
      keepTaylors = other.keepTaylors;
      num_eq_prod = other.num_eq_prod;
      deg_save = other.deg_save;
      tay_numInds = other.tay_numInds;
      tay_numDeps = other.tay_numDeps;
      numSwitches = other.numSwitches;
      nestedReverseEval = other.nestedReverseEval;
      ext_diff_fct_index = other.ext_diff_fct_index;
      nextBufferNumber = other.nextBufferNumber;
      lastTayBlockInCore = other.lastTayBlockInCore;

      delete[] signature;
      signature = std::exchange(other.signature, nullptr);
    }
    return *this;
  }
  ADOLC::detail::OpBuffer opBuffer_{};
  ADOLC::detail::ValBuffer valBuffer_{};
  ADOLC::detail::LocBuffer locBuffer_{};
  ADOLC::detail::TayBuffer tayBuffer_{};

  size_t numInds{0};
  size_t numDeps{0};
  // 1 - write taylor stack in taping mode
  int keepTaylors{0};

  size_t num_eq_prod{0};

  // degree to save and saved respectively
  int deg_save{0};
  // # of independents for the taylor stack
  size_t tay_numInds{0};
  // # of dependents for the taylor stack
  size_t tay_numDeps{0};

  size_t numSwitches{0};
  /**
   * Indicates that reverse evaluation of this tape happens inside an outer
   * tape evaluation.
   *
   * First-order reverse uses this flag to accumulate adjoints for independent
   * and dependent variables into the outer tape instead of overwriting them.
   */
  bool nestedReverseEval{false};

  /* extern diff. fcts */
  size_t ext_diff_fct_index{0}; /* set by forward and reverse (from tape) */

  // the next Buffer to read back
  size_t nextBufferNumber{0};
  // == 1 if last taylor buffer is still in
  // in core(first call of reverse)
  char lastTayBlockInCore{0};

  double *signature{nullptr};

  /**
   * @brief Load the next forward element for the tape selected by `Info`.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  typename Info::value_type loadNextForward() {
    auto &buffer = Info::getBuffer(*this);
    return buffer.readAndAdvance();
  }

  /**
   * @brief Load the next reverse element for the tape selected by `Info`.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  typename Info::value_type loadNextReverse(size_t blockSize) {
    auto &buffer = Info::getBuffer(*this);
    Info::ensureReverseReadable(*this, blockSize);
    return buffer.retreatAndRead();
  }

  // writes the block of size depth of taylor coefficients from point loc to
  // the taylor buffer, if the buffer is filled, then it is written to the
  // taylor tape
  void write_taylor(double *taylorCoefficientPos, std::ptrdiff_t keep,
                    const char *tay_fileName);

  // writes a single element (x) to the taylor buffer and writes the buffer
  // to disk if necessary
  void write_scaylor(double val, const char *tay_fileName) {
    using TayInfoT = ADOLC::detail::TayInfo<TapeRecordingContext, ErrorType>;
    if (tayBuffer_.position() == tayBuffer_.capacity())
      put_block<TayInfoT>(tay_fileName, tayBuffer_.capacity());
    tayBuffer_.writeAndAdvance(val);
  }

  /****************************************************************************/
  /* Writes the block of size depth of taylor coefficients from point loc to  */
  /* the taylor buffer.  If the buffer is filled, then it is written to the   */
  /* taylor tape.                                                             */
  /*--------------------------------------------------------------------------*/
  void write_taylors(double *taylorCoefficientPos, int keep, int degree,
                     int numDir, const char *tay_fileName);

  /****************************************************************************/
  /* Write_scaylors writes # size elements from x to the taylor buffer.       */
  /****************************************************************************/
  void write_scaylors(const double *taylorCoefficientPos, std::ptrdiff_t size,
                      const char *tay_fileName);

  /*
   * Puts a block of taylor coefficients from the value stack buffer to the
   * buffer pointed ty by taylorCoefficients. The buffer is expected to be
   * contiguous in memory. Use in Higher Order Scalar drivers.
   */
  void get_taylors(double *taylorCoefficients, std::ptrdiff_t degree,
                   size_t taylorBufferSize);

  /*
   * Puts a block of taylor coefficients from the value stack buffer to buffer
   * pointed to by taylorCoefficients. The buffer is expected to be contiguous
   * in memory. Use in Higher Order Vector drivers.
   */
  void get_taylors_p(double *taylorCoefficients, int degree, int numDir,
                     size_t taylorBufferSize);

  // functions for handling loc tape
  void put_loc(size_t loc) { locBuffer_.writeAndAdvance(loc); }

  // puts an operation into the operation buffer, ensures that location
  // buffer and constants buffer are prepared to take the belonging stuff
  void put_op(OPCODES op, const char *loc_fileName, const char *op_fileName,
              const char *val_fileName, size_t reserveExtraLocations = 0);

  /**
   * @brief Ensure that the tape file associated with Info exists and is ready
   *        for writing.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  void openFile(const char *fileName) {
    using ADOLCError::fail;
    using ADOLCError::ErrorType::CANNOT_REMOVE_FILE;
    auto &buffer = Info::getBuffer(*this);
    if (buffer.file() == nullptr) {
      if (Info::removeExistingBeforeWrite) {
        buffer.openFile(fileName, "rb");
        if (buffer.file() != nullptr) {
          buffer.closeFile();
          if (remove(fileName)) {
            fail(CANNOT_REMOVE_FILE, CURRENT_LOCATION);
          }
        }
      }
      buffer.openFile(fileName, Info::openWriteMode);
    }
  }

  /**
   * @brief Flush the current in-memory tape buffer to disk.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  void put_block(const char *fileName, size_t lengthBlock) {
    using ADOLC::detail::write;
    using ADOLCError::fail;
    using ADOLCError::ErrorType::TAPING_FATAL_IO_ERROR;

    openFile<Info>(fileName);
    const size_t numChunks = lengthBlock / Info::chunkSize;

    // write full chunks
    for (size_t chunk = 0; chunk < numChunks; chunk++) {
      auto returnCode = write<TapeRecordingContext, ErrorType, Info>(
          *this, chunk, Info::chunkSize);
      if (returnCode != 1)
        fail(TAPING_FATAL_IO_ERROR, CURRENT_LOCATION);
    }

    // write one final partial chunk
    const size_t remain = lengthBlock % Info::chunkSize;
    if (remain != 0) {
      auto returnCode = write<TapeRecordingContext, ErrorType, Info>(
          *this, numChunks, remain);
      if (returnCode != 1)
        fail(TAPING_FATAL_IO_ERROR, CURRENT_LOCATION);
    }

    auto &buffer = Info::getBuffer(*this);
    buffer.numOnTape(buffer.numOnTape() + lengthBlock);
    buffer.position(0);
  }

  /****************************************************************************/
  /* Write some constants to the buffer without disk access                   */
  /****************************************************************************/
  void put_vals_notWriteBlock(double *vals, size_t numVals) {
    for (size_t i = 0; i < numVals; ++i) {
      valBuffer_.writeAndAdvance(vals[i]);
    }
  }
  void put_vals_writeBlock(double *vals, size_t numVals,
                           const char *op_fileName, const char *val_fileName);

  /**
   * @brief Loads one block of tape data into the buffer selected by `Info`.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  void loadBlockIntoBuffer(size_t blockSize) {
    using ADOLCError::fail;
    auto &buffer = Info::getBuffer(*this);

    const size_t numChunks = blockSize / Info::chunkSize;
    for (size_t chunk = 0; chunk < numChunks; chunk++) {
      const auto ret =
          fread(buffer.begin() + (chunk * Info::chunkSize),
                Info::chunkSize * sizeof(typename Info::value_type), 1,
                buffer.file());
      if (ret != 1) {
        fail(Info::error, CURRENT_LOCATION);
      }
    }
    const size_t remain = blockSize % Info::chunkSize;
    if (remain != 0) {
      const auto ret =
          fread(buffer.begin() + (numChunks * Info::chunkSize),
                remain * sizeof(typename Info::value_type), 1, buffer.file());
      if (ret != 1) {
        fail(Info::error, CURRENT_LOCATION);
      }
    }
  }

  /**
   * @brief Load the next forward block for the tape selected by `Info`.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  void loadBlockIntoBufferForward(size_t bufferSize) {
    auto &buffer = Info::getBuffer(*this);
    const size_t blockSize = std::min(bufferSize, buffer.numOnTape());
    loadBlockIntoBuffer<Info>(blockSize);
    Info::updateBufferStatsForward(*this, blockSize);
    Info::updateBufferPositionForward(*this);
  }

  /**
   * @brief Load the next reverse block for the tape selected by `Info`.
   */
  template <InfoType<TapeRecordingContext, ErrorType> Info>
  void loadBlockIntoBufferReverse(size_t blockSize) {
    using ADOLCError::fail;

    auto &buffer = Info::getBuffer(*this);
    const long pos = Info::reverseSeekOffset(*this, blockSize);
    const auto ret = fseek(buffer.file(), pos, SEEK_SET);
    if (ret == -1) {
      fail(Info::error, CURRENT_LOCATION);
    }

    loadBlockIntoBuffer<Info>(blockSize);
    Info::updateBufferStatsReverse(*this, blockSize);
    Info::updateBufferPositionReverse(*this, blockSize);
  }

  /****************************************************************************/
  /* Returns a pointer to the first element of a values vector and skips the  */
  /* vector. -- Forward Mode --                                               */
  /****************************************************************************/
  double *get_val_v_f(size_t size) {
    double *temp = valBuffer_.current();
    valBuffer_.position(valBuffer_.position() + size);
    return temp;
  }

  /****************************************************************************/
  /* Returns a pointer to the first element of a values vector and skips the  */
  /* vector. -- Reverse Mode --                                               */
  /****************************************************************************/
  double *get_val_v_r(size_t size) {
    valBuffer_.position(valBuffer_.position() - size);
    return valBuffer_.current();
  }

  /****************************************************************************/
  /* Not sure what's going on here! -> vector class ?  --- kowarz             */
  /****************************************************************************/
  void reset_val_r(size_t valueBufferSize) {
    using ValInfoT = ADOLC::detail::ValInfo<TapeRecordingContext, ErrorType>;
    if (valBuffer_.position() == 0) {
      loadBlockIntoBufferReverse<ValInfoT>(valueBufferSize);
    }
  }

  /****************************************************************************/
  /* Update locations tape to remove assignments involving temp. variables.   */
  /* e.g.  t = a + b ; y = t  =>  y = a + b                                   */
  /****************************************************************************/
  int upd_resloc(size_t temp, size_t lhs) {
    // LocBuffer points to the first entry of the Locations and CurrLoc-1 to the
    // last placed location in the buffer. Thus, the check ask if there is no
    // element on the tape.
    if (locBuffer_.position() < 1)
      return 0;
    if (temp == locBuffer_[locBuffer_.position() - 1]) {
      locBuffer_[locBuffer_.position() - 1] = lhs;
      return 1;
    }
    return 0;
  }

  int upd_resloc_check(const size_t temp) {
    // LocBuffer points to the first entry of the Locations and CurrLoc-1 to the
    // last placed location in the buffer. Thus, the check ask if there is no
    // element on the tape.
    if (locBuffer_.position() < 1)
      return 0;
    // checks if tape-element represented by "tmp" is the last created.
    if (temp == locBuffer_[locBuffer_.position() - 1]) {
      return 1;
    }
    return 0;
  }

  /****************************************************************************/
  /* Update locations and operations tape to remove special operations inv.   */
  /* temporary variables. e.g.  t = a * b ; y += t  =>  y += a * b            */
  /****************************************************************************/
  int upd_resloc_inc_prod(size_t temp, size_t newlhs, unsigned char newop) {
    if (locBuffer_.position() < 3)
      return 0;
    if (opBuffer_.position() < 1)
      return 0;
    if (temp == locBuffer_[locBuffer_.position() - 1] &&
        mult_a_a == opBuffer_[opBuffer_.position() - 1] &&
        /* skipping recursive case */
        newlhs != locBuffer_[locBuffer_.position() - 2] &&
        newlhs != locBuffer_[locBuffer_.position() - 3]) {
      locBuffer_[locBuffer_.position() - 1] = newlhs;
      opBuffer_[opBuffer_.position() - 1] = newop;
      return 1;
    }
    return 0;
  }

  size_t get_val_space(const char *op_fileName, const char *val_fileName);
};

} // namespace ADOLC::detail

#endif // ADOLC_TAPE_RECORDING_CONTEXT_H
