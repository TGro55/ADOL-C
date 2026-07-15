
#include <adolc/valuetape/tapeevaluationcontext.h>
#include <span>

namespace ADOLC::detail {

void TapeEvaluationContext::discard_params_r(size_t valueBufferSize,
                                             size_t numParam) {
  constexpr size_t chunkSize = ADOLC_IO_CHUNK_SIZE / sizeof(double);
  size_t ip = numParam;
  while (ip > 0) {
    size_t rsize = valBuffer_.position();
    rsize = (rsize < ip) ? rsize : ip;
    ip -= rsize;
    valBuffer_.position(valBuffer_.position() - rsize);
    if (ip > 0) {
      fseek(valBuffer_.file(),
            static_cast<long>(sizeof(double) *
                              (valBuffer_.numOnTape() - valueBufferSize)),
            SEEK_SET);

      const size_t chunks = valueBufferSize / chunkSize;
      for (size_t i = 0; i < chunks; ++i)
        if (fread(valBuffer_.begin() + i * chunkSize,
                  chunkSize * sizeof(double), 1, valBuffer_.file()) != 1)
          ADOLCError::fail(ADOLCError::ErrorType::VAL_READ_FAILED,
                           CURRENT_LOCATION);

      const size_t remain = valueBufferSize % chunkSize;
      if (remain != 0)
        if (fread(valBuffer_.begin() + chunks * chunkSize,
                  remain * sizeof(double), 1, valBuffer_.file()) != 1)
          ADOLCError::fail(ADOLCError::ErrorType::VAL_READ_FAILED,
                           CURRENT_LOCATION);

      valBuffer_.numOnTape(valBuffer_.numOnTape() - valueBufferSize);
      valBuffer_.position(valBuffer_.capacity());
    }
  }
}

void TapeEvaluationContext::taylor_back(size_t taylorBufferSize, short tapeId,
                                        const char *tay_fileName) {
  if (tayBuffer_.begin() == nullptr)
    ADOLCError::fail(ADOLCError::ErrorType::REVERSE_NO_TAYLOR_STACK,
                     CURRENT_LOCATION, ADOLCError::FailInfo{.info1 = tapeId});

  nextBufferNumber = tayBuffer_.numOnTape() / taylorBufferSize;
  size_t number = tayBuffer_.numOnTape() % taylorBufferSize;
  if (number == 0 && tayBuffer_.numOnTape() != 0) {
    number = taylorBufferSize;
    --nextBufferNumber;
  }
  tayBuffer_.position(number);

  const bool hasPreviousBlock = nextBufferNumber > 0;
  if ((lastTayBlockInCore != 1 || hasPreviousBlock) &&
      tayBuffer_.file() == nullptr)
    tayBuffer_.openFile(tay_fileName, "rb");

  if (lastTayBlockInCore != 1) {
    if (tayBuffer_.file() == nullptr)
      ADOLCError::fail(ADOLCError::ErrorType::TAY_NULLPTR, CURRENT_LOCATION);

    if (fseek(tayBuffer_.file(),
              static_cast<long>(sizeof(double) * nextBufferNumber *
                                taylorBufferSize),
              SEEK_SET) == -1)
      ADOLCError::fail(ADOLCError::ErrorType::EVAL_SEEK_VALUE_STACK,
                       CURRENT_LOCATION);

    constexpr size_t chunkSize = ADOLC_IO_CHUNK_SIZE / sizeof(double);
    const size_t chunks = number / chunkSize;

    for (size_t i = 0; i < chunks; ++i)
      if (fread(tayBuffer_.begin() + i * chunkSize, chunkSize * sizeof(double),
                1, tayBuffer_.file()) != 1)
        ADOLCError::fail(ADOLCError::ErrorType::TAPING_FATAL_IO_ERROR,
                         CURRENT_LOCATION);

    const size_t remain = number % chunkSize;
    if (remain != 0)
      if (fread(tayBuffer_.begin() + chunks * chunkSize,
                remain * sizeof(double), 1, tayBuffer_.file()) != 1)
        ADOLCError::fail(ADOLCError::ErrorType::TAPING_FATAL_IO_ERROR,
                         CURRENT_LOCATION);
  }
  if (hasPreviousBlock)
    --nextBufferNumber;
}

void TapeEvaluationContext::write_taylor(double *taylorCoefficientPos,
                                         std::ptrdiff_t keep,
                                         const char *tay_fileName) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeEvaluationContext, ErrorType>;
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

void TapeEvaluationContext::get_taylors(double *taylorCoefficients,
                                        std::ptrdiff_t degree,
                                        size_t taylorBufferSize) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeEvaluationContext, ErrorType>;
  double *T = taylorCoefficients + degree;
  while (tayBuffer_.position() < static_cast<size_t>(degree)) {
    std::span<double> taySpan(tayBuffer_.begin(), tayBuffer_.current());
    for (auto tay = taySpan.rbegin(); tay != taySpan.rend(); tay++)
      *(T--) = *tay;
    degree -= tayBuffer_.position();
    loadBlockIntoBufferReverse<TayInfoT>(taylorBufferSize);
  }

  for (int j = 0; j < degree; ++j)
    *(--T) = tayBuffer_.retreatAndRead();
}
void TapeEvaluationContext::get_taylors_p(double *taylorCoefficients,
                                          int degree, int numDir,
                                          size_t taylorBufferSize) {
  using TayInfoT = ADOLC::detail::TayInfo<TapeEvaluationContext, ErrorType>;
  double *T = taylorCoefficients + (static_cast<ptrdiff_t>(degree * numDir));

  for (int j = 0; j < numDir; ++j) {
    for (int i = 1; i < degree; ++i) {
      if (tayBuffer_.position() == 0)
        loadBlockIntoBufferReverse<TayInfoT>(taylorBufferSize);

      --T;
      *T = tayBuffer_.retreatAndRead();
    }
    --T;
  }

  if (tayBuffer_.position() == 0)
    loadBlockIntoBufferReverse<TayInfoT>(taylorBufferSize);

  tayBuffer_.retreat();
  for (int i = 0; i < numDir; ++i) {
    *T = *tayBuffer_.current();
    T += degree;
  }
}
} // namespace ADOLC::detail