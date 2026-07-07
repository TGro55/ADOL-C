#include <adolc/adolcerror.h>
#include <adolc/valuetape/infotype.h>
#include <adolc/valuetape/tapeinfos.h>
#include <adolc/valuetape/taperecordingcontext.h>
#include <cstddef>
#include <cstring> // for memset
#include <span>

namespace ADOLC::detail {

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
      locBuffer_.capacity() - TapeInfos::maxLocsPerOp - reserveExtraLocations) {
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
