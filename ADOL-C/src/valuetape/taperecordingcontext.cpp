#include <adolc/adolcerror.h>
#include <adolc/valuetape/infotype.h>
#include <adolc/valuetape/tapeinfos.h>
#include <adolc/valuetape/taperecordingcontext.h>
#include <cstddef>
#include <cstring> // for memset
#include <span>

namespace ADOLC::detail {

/****************************************************************************/
/* Writes the block of size depth of taylor coefficients from point loc to  */
/* the taylor buffer. If the buffer is filled, then it is written to the    */
/* taylor tape.                                                             */
/****************************************************************************/
void TapeRecordingContext::write_taylor(double *taylorCoefficientPos,
                                        std::ptrdiff_t keep,
                                        const char *tay_fileName) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeRecordingContext, ErrorType>;
  size_t remaining = static_cast<size_t>(keep);
  while (remaining > tayBuffer_.remainingCapacity()) {
    for (auto i = tayBuffer_.position(); i < tayBuffer_.capacity(); ++i) {
      tayBuffer_[i] = *taylorCoefficientPos;
      ++taylorCoefficientPos;
    }
    remaining -= tayBuffer_.remainingCapacity();
    put_block<TayInfoT>(tay_fileName, tayBuffer_.capacity());
  }

  for (size_t i = 0; i < remaining; ++i) {
    tayBuffer_[i + tayBuffer_.position()] = *taylorCoefficientPos;
    ++taylorCoefficientPos;
  }
  tayBuffer_.position(tayBuffer_.position() + remaining);
}

void TapeRecordingContext::write_taylors(double *taylorCoefficientPos, int keep,
                                         int degree, int numDir,
                                         const char *tay_fileName) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeRecordingContext, ErrorType>;
  for (int j = 0; j < numDir; ++j) {
    for (int i = 0; i < keep; ++i) {
      if (tayBuffer_.position() == tayBuffer_.capacity()) {
        put_block<TayInfoT>(tay_fileName, tayBuffer_.capacity());
      }

      tayBuffer_.writeAndAdvance(*taylorCoefficientPos);
      ++taylorCoefficientPos;
    }
    if (degree > keep)
      taylorCoefficientPos += degree - keep;
  }
}

void TapeRecordingContext::write_scaylors(const double *taylorCoefficientPos,
                                          std::ptrdiff_t size,
                                          const char *tay_fileName) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeRecordingContext, ErrorType>;

  auto remaining = static_cast<size_t>(size);
  size_t pos = 0;
  while (remaining > tayBuffer_.remainingCapacity()) {
    std::span<double> taySpan(tayBuffer_.current(),
                              tayBuffer_.begin() + tayBuffer_.capacity());
    for (double &tay : taySpan) {
      tay = taylorCoefficientPos[pos++];
    }
    remaining -= tayBuffer_.remainingCapacity();
    put_block<TayInfoT>(tay_fileName, tayBuffer_.capacity());
  }

  std::span<double> tayBufferSpan(tayBuffer_.current(),
                                  tayBuffer_.current() + remaining);
  for (double &tay : tayBufferSpan) {
    tay = taylorCoefficientPos[pos++];
  }
  tayBuffer_.position(tayBuffer_.position() + remaining);
}

void TapeRecordingContext::get_taylors(double *taylorCoefficients,
                                       std::ptrdiff_t degree,
                                       size_t taylorBufferSize) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeRecordingContext, ErrorType>;
  double *T = taylorCoefficients + degree;
  while (tayBuffer_.position() < static_cast<size_t>(degree)) {
    std::span<double> taySpan(tayBuffer_.begin(), tayBuffer_.current());
    for (auto tay = taySpan.rbegin(); tay != taySpan.rend(); tay++) {
      *(T--) = *tay;
    }
    degree -= tayBuffer_.position();
    loadBlockIntoBufferReverse<TayInfoT>(taylorBufferSize);
  }

  /* Copy the remaining values from the stack into the buffer ... */
  for (int j = 0; j < degree; ++j) {
    *(--T) = tayBuffer_.retreatAndRead();
  }
}

void TapeRecordingContext::get_taylors_p(double *taylorCoefficients, int degree,
                                         int numDir, size_t taylorBufferSize) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeRecordingContext, ErrorType>;
  double *T = taylorCoefficients + (static_cast<ptrdiff_t>(degree * numDir));

  /* update the directions except the base point parts */
  for (int j = 0; j < numDir; ++j) {
    for (int i = 1; i < degree; ++i) {
      if (tayBuffer_.position() == 0) {
        loadBlockIntoBufferReverse<TayInfoT>(taylorBufferSize);
      }

      --T;
      *T = tayBuffer_.retreatAndRead();
    }
    --T; /* skip the base point part */
  }
  /* now update the base point parts */
  if (tayBuffer_.position() == 0) {
    loadBlockIntoBufferReverse<TayInfoT>(taylorBufferSize);
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
void TapeRecordingContext::put_op(OPCODES op, const char *loc_fileName,
                                  const char *op_fileName,
                                  const char *val_fileName,
                                  size_t reserveExtraLocations) {
  using LocInfoT = LocInfo<TapeRecordingContext, ErrorType>;
  using OpInfoT = OpInfo<TapeRecordingContext, ErrorType>;
  using ValInfoT = ValInfo<TapeRecordingContext, ErrorType>;

  /* make sure we have enough slots to write the locs */
  if (locBuffer_.position() >
      locBuffer_.capacity() - maxLocsPerOp - reserveExtraLocations) {
    const size_t remainder = locBuffer_.remainingCapacity();
    if (remainder > 0)
      std::memset(locBuffer_.current(), 0, (remainder - 1) * sizeof(size_t));
    locBuffer_[locBuffer_.capacity() - 1] = remainder;
    put_block<LocInfoT>(loc_fileName, locBuffer_.capacity());
    /* every operation writes 1 opcode */
    if (opBuffer_.position() == opBuffer_.capacity() - 1) {
      opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
      put_block<OpInfoT>(op_fileName, opBuffer_.capacity());
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
    put_block<ValInfoT>(val_fileName, valBuffer_.capacity());
    /* every operation writes 1 opcode */
    if (opBuffer_.position() == opBuffer_.capacity() - 1) {
      opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
      put_block<OpInfoT>(op_fileName, opBuffer_.capacity());
      opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
    }
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_val));
  }
  /* every operation writes 1 opcode */
  if (opBuffer_.position() == opBuffer_.capacity() - 1) {
    opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
    put_block<OpInfoT>(op_fileName, opBuffer_.capacity());
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
void TapeRecordingContext::put_vals_writeBlock(double *vals, size_t numVals,
                                               const char *op_fileName,
                                               const char *val_fileName) {
  using OpInfoT = OpInfo<TapeRecordingContext, ErrorType>;
  using ValInfoT = ValInfo<TapeRecordingContext, ErrorType>;
  for (size_t i = 0; i < numVals; ++i) {
    valBuffer_.writeAndAdvance(vals[i]);
  }
  put_loc(valBuffer_.capacity() - valBuffer_.position());
  put_block<ValInfoT>(val_fileName, valBuffer_.capacity());
  /* every operation writes 1 opcode */
  if (opBuffer_.position() == opBuffer_.capacity() - 1) {
    opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
    put_block<OpInfoT>(op_fileName, opBuffer_.capacity());
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
  }
  opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_val));
}

/****************************************************************************/
/* Returns the number of free constants in the real tape. Ensures that it   */
/* is at least 5.                                                           */
/****************************************************************************/
size_t TapeRecordingContext::get_val_space(const char *op_fileName,
                                           const char *val_fileName) {
  using OpInfoT = OpInfo<TapeRecordingContext, ErrorType>;
  using ValInfoT = ValInfo<TapeRecordingContext, ErrorType>;

  if (valBuffer_.position() > valBuffer_.capacity() - 5) {
    put_loc(valBuffer_.remainingCapacity());
    put_block<ValInfoT>(val_fileName, valBuffer_.capacity());
    /* every operation writes 1 opcode */
    if (opBuffer_.position() == opBuffer_.capacity() - 1) {
      opBuffer_.writeCurrent(static_cast<unsigned char>(end_of_op));
      put_block<OpInfoT>(op_fileName, opBuffer_.capacity());
      opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_op));
    }
    opBuffer_.writeAndAdvance(static_cast<unsigned char>(end_of_val));
  }
  return (valBuffer_.remainingCapacity());
}

} // namespace ADOLC::detail
