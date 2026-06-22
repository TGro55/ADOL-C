#include <adolc/adolcerror.h>
#include <adolc/valuetape/infotype.h>
#include <adolc/valuetape/tapeinfos.h>
#include <cstddef>
#include <cstring> // for memset
#include <span>

TapeInfos::TapeInfos(short tapeId) { tapeId_ = tapeId; }

TapeInfos::~TapeInfos() {
  delete[] signature;
  signature = nullptr;
}

TapeInfos::TapeInfos(TapeInfos &&other) noexcept
    : opBuffer_(std::move(other.opBuffer_)),
      valBuffer_(std::move(other.valBuffer_)),
      locBuffer_(std::move(other.locBuffer_)),
      tayBuffer_(std::move(other.tayBuffer_)), stats(other.stats),
      tapeId_(other.tapeId_), numInds(other.numInds), numDeps(other.numDeps),
      keepTaylors(other.keepTaylors), num_eq_prod(other.num_eq_prod),
      nextBufferNumber(other.nextBufferNumber),
      lastTayBlockInCore(other.lastTayBlockInCore), deg_save(other.deg_save),
      tay_numInds(other.tay_numInds), tay_numDeps(other.tay_numDeps),
      workMode(other.workMode), ext_diff_fct_index(other.ext_diff_fct_index),
      nestedReverseEval(other.nestedReverseEval),
      numSwitches(other.numSwitches), signature(other.signature) {
  other.signature = nullptr;
}

TapeInfos &TapeInfos::operator=(TapeInfos &&other) noexcept {
  if (this != &other) {
    // Free existing resources to avoid leaks
    delete[] signature;

    // **2. Move data members**
    tapeId_ = other.tapeId_;
    numInds = other.numInds;
    numDeps = other.numDeps;
    keepTaylors = other.keepTaylors;
    std::copy(std::begin(other.stats), std::end(other.stats),
              std::begin(stats));

    num_eq_prod = other.num_eq_prod;
    opBuffer_ = std::move(other.opBuffer_);
    valBuffer_ = std::move(other.valBuffer_);
    locBuffer_ = std::move(other.locBuffer_);
    tayBuffer_ = std::move(other.tayBuffer_);
    nextBufferNumber = other.nextBufferNumber;
    lastTayBlockInCore = other.lastTayBlockInCore;

    deg_save = other.deg_save;
    tay_numInds = other.tay_numInds;
    tay_numDeps = other.tay_numDeps;

    workMode = other.workMode;
    ext_diff_fct_index = other.ext_diff_fct_index;
    nestedReverseEval = other.nestedReverseEval;
    numSwitches = other.numSwitches;
    signature = other.signature;

    // **3. Null out source object’s pointers to prevent double deletion**
    other.signature = nullptr;
  }
  return *this;
}

void TapeInfos::freeTapeResources() {
  opBuffer_ = {};
  valBuffer_ = {};
  locBuffer_ = {};
  tayBuffer_ = {};
  if (signature) {
    delete[] signature;
    signature = nullptr;
  }
}

/****************************************************************************/
/* Writes the block of size depth of taylor coefficients from point loc to  */
/* the taylor buffer. If the buffer is filled, then it is written to the    */
/* taylor tape.                                                             */
/****************************************************************************/
void TapeInfos::write_taylor(double *taylorCoefficientPos, std::ptrdiff_t keep,
                             const char *tay_fileName) {
  using TayInfo = ADOLC::detail::TayInfo<TapeInfos, ErrorType>;
  while (tayBuffer_.position() > tayBuffer_.capacity() - keep) {
    for (auto i = tayBuffer_.position(); i < tayBuffer_.capacity(); ++i) {
      tayBuffer_[i] = *taylorCoefficientPos;
      ++taylorCoefficientPos;
    }
    keep -= tayBuffer_.remainingCapacity();
    put_block<TayInfo>(tay_fileName, tayBuffer_.capacity());
  }

  for (int i = 0; i < keep; ++i) {
    tayBuffer_[i + tayBuffer_.position()] = *taylorCoefficientPos;
    ++taylorCoefficientPos;
  }
  tayBuffer_.position(tayBuffer_.position() + keep);
}

void TapeInfos::write_taylors(double *taylorCoefficientPos, int keep,
                              int degree, int numDir,
                              const char *tay_fileName) {
  using TayInfo = ADOLC::detail::TayInfo<TapeInfos, ErrorType>;
  for (int j = 0; j < numDir; ++j) {
    for (int i = 0; i < keep; ++i) {
      if (tayBuffer_.position() == tayBuffer_.capacity()) {
        put_block<TayInfo>(tay_fileName, tayBuffer_.capacity());
      }

      tayBuffer_.writeAndAdvance(*taylorCoefficientPos);
      ++taylorCoefficientPos;
    }
    if (degree > keep)
      taylorCoefficientPos += degree - keep;
  }
}

void TapeInfos::write_scaylors(const double *taylorCoefficientPos,
                               std::ptrdiff_t size, const char *tay_fileName) {
  using TayInfo = ADOLC::detail::TayInfo<TapeInfos, ErrorType>;
  size_t pos = 0;
  while (tayBuffer_.position() > tayBuffer_.capacity() - size) {
    std::span<double> taySpan(tayBuffer_.current(),
                              tayBuffer_.begin() + tayBuffer_.capacity());
    for (double &tay : taySpan) {
      tay = taylorCoefficientPos[pos++];
    }
    size -= tayBuffer_.remainingCapacity();
    put_block<TayInfo>(tay_fileName, tayBuffer_.capacity());
  }

  std::span<double> tayBufferSpan(tayBuffer_.current(),
                                  tayBuffer_.current() + size);
  for (double &tay : tayBufferSpan) {
    tay = taylorCoefficientPos[pos++];
  }
  tayBuffer_.position(tayBuffer_.position() + size);
}

void TapeInfos::get_taylors(double *taylorCoefficients, std::ptrdiff_t degree) {
  using TayInfo = ADOLC::detail::TayInfo<TapeInfos, ErrorType>;
  double *T = taylorCoefficients + degree;
  while (tayBuffer_.position() < static_cast<size_t>(degree)) {
    std::span<double> taySpan(tayBuffer_.begin(), tayBuffer_.current());
    for (auto tay = taySpan.rbegin(); tay != taySpan.rend(); tay++) {
      *(T--) = *tay;
    }
    degree -= tayBuffer_.position();
    loadBlockIntoBufferReverse<TayInfo>();
  }

  /* Copy the remaining values from the stack into the buffer ... */
  for (int j = 0; j < degree; ++j) {
    *(--T) = tayBuffer_.retreatAndRead();
  }
}

void TapeInfos::get_taylors_p(double *taylorCoefficients, int degree,
                              int numDir) {
  using TayInfo = ADOLC::detail::TayInfo<TapeInfos, ErrorType>;
  double *T = taylorCoefficients + (static_cast<ptrdiff_t>(degree * numDir));

  /* update the directions except the base point parts */
  for (int j = 0; j < numDir; ++j) {
    for (int i = 1; i < degree; ++i) {
      if (tayBuffer_.position() == 0) {
        loadBlockIntoBufferReverse<TayInfo>();
      }

      --T;
      *T = tayBuffer_.retreatAndRead();
    }
    --T; /* skip the base point part */
  }
  /* now update the base point parts */
  if (tayBuffer_.position() == 0) {
    loadBlockIntoBufferReverse<TayInfo>();
  }

  tayBuffer_.retreat();
  for (int i = 0; i < numDir; ++i) {
    *T = *tayBuffer_.current();
    T += degree;
  }
}

/**
 * Functions for handling operations tape
 */
/****************************************************************************/
/* Puts an operation into the operation buffer. Ensures that location buffer*/
/* and constants buffer are prepared to take the belonging stuff.           */
/****************************************************************************/
void TapeInfos::put_op(OPCODES op, const char *loc_fileName,
                       const char *op_fileName, const char *val_fileName,
                       size_t reserveExtraLocations) {
  using ADOLC::detail::LocInfo;
  using ADOLC::detail::OpInfo;
  using ADOLC::detail::ValInfo;
  using ADOLCError::ErrorType;
  /* make sure we have enough slots to write the locs */
  if (locBuffer_.position() >
      locBuffer_.capacity() - maxLocsPerOp - reserveExtraLocations) {
    const size_t remainder = locBuffer_.remainingCapacity();
    if (remainder > 0)
      std::memset(locBuffer_.current(), 0, (remainder - 1) * sizeof(size_t));
    locBuffer_[locBuffer_.capacity() - 1] = remainder;
    put_block<LocInfo<TapeInfos, ErrorType>>(loc_fileName,
                                             locBuffer_.capacity());
    /* every operation writes 1 opcode */
    if (opBuffer_.position() == opBuffer_.capacity() - 1) {
      opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
      put_block<OpInfo<TapeInfos, ErrorType>>(op_fileName,
                                              opBuffer_.capacity());
      opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
    }
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_int));
  }
  /* every operation writes <5 values --- 3 should be sufficient */
  if (valBuffer_.position() > valBuffer_.capacity() - 5) {
    const size_t valRemainder = valBuffer_.remainingCapacity();
    put_loc(valRemainder);
    /* avoid writing uninitialized memory to the file and get valgrind upset
     */
    std::memset(valBuffer_.current(), 0, valRemainder * sizeof(double));
    put_block<ValInfo<TapeInfos, ErrorType>>(val_fileName,
                                             valBuffer_.capacity());
    /* every operation writes 1 opcode */
    if (opBuffer_.position() == opBuffer_.capacity() - 1) {
      opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
      put_block<OpInfo<TapeInfos, ErrorType>>(op_fileName,
                                              opBuffer_.capacity());
      opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
    }
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_val));
  }
  /* every operation writes 1 opcode */
  if (opBuffer_.position() == opBuffer_.capacity() - 1) {
    opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
    put_block<OpInfo<TapeInfos, ErrorType>>(op_fileName, opBuffer_.capacity());
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
  }
  opBuffer_.writeAndAdvance(static_cast<unsigned char>(op));
}

/**
 * functions for handling the value tape
 *
 *
 */

/****************************************************************************/
/* Writes a block of constants (real) onto hard disk and handles file       */
/* creation, removal, ...                                                   */
/****************************************************************************/
void TapeInfos::put_vals_writeBlock(double *vals, size_t numVals,
                                    const char *op_fileName,
                                    const char *val_fileName) {
  using ADOLC::detail::OpInfo;
  using ADOLC::detail::ValInfo;
  using ADOLCError::ErrorType;
  for (size_t i = 0; i < numVals; ++i) {
    valBuffer_.writeAndAdvance(vals[i]);
  }
  put_loc(valBuffer_.capacity() - valBuffer_.position());
  put_block<ValInfo<TapeInfos, ErrorType>>(val_fileName, valBuffer_.capacity());
  /* every operation writes 1 opcode */
  if (opBuffer_.position() == opBuffer_.capacity() - 1) {
    opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
    put_block<OpInfo<TapeInfos, ErrorType>>(op_fileName, opBuffer_.capacity());
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
  }
  opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_val));
}

/****************************************************************************/
/* Returns the number of free constants in the real tape. Ensures that it   */
/* is at least 5.                                                           */
/****************************************************************************/
size_t TapeInfos::get_val_space(const char *op_fileName,
                                const char *val_fileName) {

  using ADOLC::detail::OpInfo;
  using ADOLC::detail::ValInfo;
  using ADOLCError::ErrorType;

  if (valBuffer_.position() > valBuffer_.capacity() - 5) {
    put_loc(valBuffer_.remainingCapacity());
    put_block<ValInfo<TapeInfos, ErrorType>>(val_fileName,
                                             valBuffer_.capacity());
    /* every operation writes 1 opcode */
    if (opBuffer_.position() == opBuffer_.capacity() - 1) {
      opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
      put_block<OpInfo<TapeInfos, ErrorType>>(op_fileName,
                                              opBuffer_.capacity());
      opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
    }
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_val));
  }
  return (valBuffer_.remainingCapacity());
}
