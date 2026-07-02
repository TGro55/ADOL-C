#ifndef ADOLC_TAPEINFOS_H
#define ADOLC_TAPEINFOS_H

#include <adolc/adalloc.h>
#include <adolc/adolcerror.h>
#include <adolc/oplate.h>
#include <adolc/valuetape/bufferstate.h>
#include <array>
#include <memory>

using ADOLCError::ErrorType;
struct TapeInfos {

  // named indices of to print out value tape stats
  enum StatEntries {
    NUM_INDEPENDENTS, /* # of independent variables */
    NUM_DEPENDENTS,   /* # of dependent variables */
    NUM_MAX_LIVES,    /* max # of live variables */
    NUM_TAYS,         /* # of values in the taylor (value) stack */
    OP_BUFFER_SIZE,  /* # of operations per buffer == OBUFSIZE   (usrparms.h) */
    NUM_OPERATIONS,  /* overall # of operations */
    OP_FILE_ACCESS,  /* operations file written or not */
    NUM_LOCATIONS,   /* overall # of locations */
    LOC_FILE_ACCESS, /* locations file written or not */
    NUM_VALUES,      /* overall # of values */
    VAL_FILE_ACCESS, /* values file written or not */
    LOC_BUFFER_SIZE, /* # of locations per buffer == LBUFSIZE (usrparms.h) */
    VAL_BUFFER_SIZE, /* # of values per buffer == CBUFSIZE(usrparms.h) */
    TAY_BUFFER_SIZE, /* # of taylors per buffer <= TBUFSIZE (usrparms.h) */
    NUM_EQ_PROD,     /* # of eq_*_prod for sparsity pattern */
    NO_MIN_MAX,   /* no use of min_op, deferred to abs_op for piecewise stuff */
    NUM_SWITCHES, /* # of abs calls that can switch branch */
    NUM_PARAM, /* no of parameters (doubles) interchangeable without retaping */
    STAT_SIZE  /* represents the size of the stats vector */
  };
  // modes for the tape evaluation; set by functions like "fos_forward"
  enum WORKMODES { NO_MODE, WRITE_ACCESS, READ_ACCESS };

  ~TapeInfos() = default;
  TapeInfos() = default;
  TapeInfos(short tapeId) : tapeId_(tapeId) {};
  TapeInfos(const TapeInfos &) = default;
  TapeInfos &operator=(const TapeInfos &) = default;
  TapeInfos(TapeInfos &&other) noexcept = default;
  TapeInfos &operator=(TapeInfos &&other) noexcept = default;

  std::array<size_t, STAT_SIZE> stats{};

  short tapeId_{-1};
  WORKMODES workMode{NO_MODE};

  constexpr static size_t maxLocsPerOp{10}; // used in tape_loc_...
};

#endif // ADOLC_TAPEINFOS_H
