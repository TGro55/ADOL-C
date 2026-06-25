#ifndef ADOLC_INFO_TYPE_H
#define ADOLC_INFO_TYPE_H
#include <adolc/adolcerror.h>
#include <adolc/dvlparms.h>
#include <adolc/internal/usrparms.h> // ADOLC_IO_CHUNK_SIZE
#include <adolc/valuetape/bufferstate.h>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <utility>

/*
  This header defines a small “policy” interface used by ADOL-C tape I/O code.

  The idea:
    - Different tapes (op/loc/val/tay) store their data in different fields of a
      TInfos object (buffers, file handles, counters, etc.).
    - The tape-handling algorithms want one uniform API to access those fields.
    - The *Info structs* (OpInfo, LocInfo, ValInfo, TayInfo) provide that API.
    - If a check about how many file accesses has to be done InfoType is used,
  otherwise the less restrictive InfoTypeBase

  Usage pattern (typical):
    template<class Info, class TInfos, class Err>
      requires ADOLC::detail::InfoTypeBase<Info, TInfos, Err>
    void do_something(TInfos& ti, ...);

  Each Info struct is a stateless “adapter” that:
    - names the element type stored in the buffer (Info::value_type)
    - exposes bookkeeping constants (Info::num, Info::bufferSize, ...)
    - selects the concrete buffer stored in TInfos
    - provides buffer specific hooks used during tape sweeps
*/

namespace ADOLC::detail {

template <class Info, class TInfos>
using InfoBufferType =
    std::remove_cvref_t<decltype(Info::getBuffer(std::declval<TInfos &>()))>;

/**
 * @brief Concept describing the required interface of a tape “Info” adapter.
 *
 * @tparam T         The adapter type (e.g. OpInfo<TInfos, EType>).
 * @tparam TInfos    The tape state/metadata type that stores buffers, counters,
 *                   file handles, etc. (the adapter maps into these fields).
 * @tparam ErrorType The error enum/type used by higher-level code.
 */
template <class T, class TInfos, class ErrorType>
concept InfoTypeBase = requires(
    TInfos &tapeInfos, const TInfos &constTapeInfos,
    const std::array<size_t, TInfos::STAT_SIZE> &stats, const char *fileName) {
  typename T::value_type;
  requires BufferStateType<InfoBufferType<T, TInfos>, typename T::value_type>;

  { T::fileIndex } -> std::convertible_to<size_t>;
  { T::num } -> std::same_as<const typename TInfos::StatEntries &>;
  { T::bufferSize } -> std::same_as<const typename TInfos::StatEntries &>;
  { T::error } -> std::convertible_to<ErrorType>;
  { T::chunkSize } -> std::convertible_to<size_t>;
  { T::removeExistingBeforeWrite } -> std::convertible_to<bool>;
  { T::openWriteMode } -> std::convertible_to<const char *>;

  { T::getBuffer(tapeInfos) } -> std::same_as<InfoBufferType<T, TInfos> &>;
  {
    T::getBuffer(constTapeInfos)
  } -> std::same_as<const InfoBufferType<T, TInfos> &>;

  T::ensureReverseReadable(tapeInfos);
  { T::reverseSeekOffset(constTapeInfos, stats) } -> std::convertible_to<long>;
  T::updateBufferStatsForward(tapeInfos, size_t{});
  T::updateBufferStatsReverse(tapeInfos, size_t{});
  T::updateBufferPositionForward(tapeInfos);
  T::updateBufferPositionReverse(tapeInfos, size_t{});
  T::prepareForwardPosition(tapeInfos, stats);
};

template <class T, class TInfos>
concept HasFileAccessEntry = requires {
  { T::fileAccess } -> std::same_as<const typename TInfos::StatEntries &>;
};

/**
 * @brief Adds to the concept InfoTypeBase the check for file access counters.
 */
template <class T, class TInfos, class ErrorType>
concept InfoType =
    InfoTypeBase<T, TInfos, ErrorType> && HasFileAccessEntry<T, TInfos>;

template <class Derived, class TInfos, typename ValueType>
struct InfoAdapterBase {
  using value_type = ValueType;
  using StatEntries = typename TInfos::StatEntries;
  using StatsArray = std::array<size_t, TInfos::STAT_SIZE>;

  static constexpr bool removeExistingBeforeWrite = true;
  static constexpr const char *openWriteMode = "wb";

  // Special case for taylor coefficients because there is no "last taylor
  // coeff" marker as it is for locs and vals
  static void ensureReverseReadable(TInfos & /*tapeInfos*/) {}
  static long reverseSeekOffset(const TInfos &tapeInfos,
                                const StatsArray &stats) {
    const auto &buffer = Derived::getBuffer(tapeInfos);
    const auto blockSize = stats[Derived::bufferSize];
    return static_cast<long>(sizeof(value_type) *
                             (buffer.numOnTape() - blockSize));
  }
  static void updateBufferStatsForward(TInfos &tapeInfos, size_t blockSize) {
    auto &buffer = Derived::getBuffer(tapeInfos);
    buffer.numOnTape(buffer.numOnTape() - blockSize);
  }

  static void updateBufferStatsReverse(TInfos &tapeInfos, size_t blockSize) {
    auto &buffer = Derived::getBuffer(tapeInfos);
    buffer.numOnTape(buffer.numOnTape() - blockSize);
  }

  static void updateBufferPositionForward(TInfos &tapeInfos) {
    Derived::getBuffer(tapeInfos).position(0);
  }

  static void updateBufferPositionReverse(TInfos &tapeInfos, size_t blockSize) {
    Derived::getBuffer(tapeInfos).position(blockSize);
  }

  static void prepareForwardPosition(TInfos &tapeInfos,
                                     const StatsArray & /*unused*/) {
    Derived::getBuffer(tapeInfos).position(0);
  }
};

/// Wrapper of fwrite
template <typename TInfos, typename ErrorType,
          InfoTypeBase<TInfos, ErrorType> Info>
static size_t write(TInfos &tapeInfos, size_t chunk, size_t size) {
  auto &buffer = Info::getBuffer(tapeInfos);
  return fwrite(buffer.begin() + (chunk * Info::chunkSize),
                size * sizeof(typename Info::value_type), 1, buffer.file());
}

/**
 * @brief Adapter for the operations tape (op tape). Fulfills InfoType.
 */
template <class TInfos, class EType>
struct OpInfo : InfoAdapterBase<OpInfo<TInfos, EType>, TInfos, unsigned char> {
  using Base = InfoAdapterBase<OpInfo<TInfos, EType>, TInfos, unsigned char>;
  using value_type = typename Base::value_type;
  using StatEntries = typename Base::StatEntries;

  static const StatEntries num = TInfos::NUM_OPERATIONS;
  static const StatEntries fileAccess = TInfos::OP_FILE_ACCESS;
  static const StatEntries bufferSize = TInfos::OP_BUFFER_SIZE;
  static constexpr size_t fileIndex = 0;

  static constexpr EType error = EType::OP_READ_FAILED;
  static constexpr size_t chunkSize = ADOLC_IO_CHUNK_SIZE / sizeof(value_type);

  static OpBuffer &getBuffer(TInfos &tapeInfos) { return tapeInfos.opBuffer_; }
  static const OpBuffer &getBuffer(const TInfos &tapeInfos) {
    return tapeInfos.opBuffer_;
  }
};

/**
 * @brief Adapter for the locations tape (loc tape). Fulfills InfoType.
 */
template <class TInfos, class EType>
struct LocInfo : InfoAdapterBase<LocInfo<TInfos, EType>, TInfos, size_t> {
  using Self = LocInfo<TInfos, EType>;
  using Base = InfoAdapterBase<Self, TInfos, size_t>;
  using value_type = typename Base::value_type;
  using StatEntries = typename Base::StatEntries;
  using StatsArray = typename Base::StatsArray;

  static const StatEntries num = TInfos::NUM_LOCATIONS;
  static const StatEntries fileAccess = TInfos::LOC_FILE_ACCESS;
  static const StatEntries bufferSize = TInfos::LOC_BUFFER_SIZE;
  static constexpr size_t fileIndex = 1;

  static constexpr EType error = EType::LOC_READ_FAILED;
  static constexpr size_t chunkSize = ADOLC_IO_CHUNK_SIZE / sizeof(value_type);

  static LocBuffer &getBuffer(TInfos &tapeInfos) {
    return tapeInfos.locBuffer_;
  }
  static const LocBuffer &getBuffer(const TInfos &tapeInfos) {
    return tapeInfos.locBuffer_;
  }

  static void updateBufferPositionReverse(TInfos &tapeInfos, size_t blockSize) {
    auto &buffer = getBuffer(tapeInfos);
    const auto loc = blockSize - buffer[blockSize - 1];
    buffer.position(loc);
  }

  static void prepareForwardPosition(TInfos &tapeInfos,
                                     const StatsArray &stats) {
    size_t numLocsForStats = statSpace;
    while (numLocsForStats >= stats[bufferSize]) {
      tapeInfos.template loadBlockIntoBufferForward<Self>();
      numLocsForStats -= stats[bufferSize];
    }
    getBuffer(tapeInfos).position(numLocsForStats);
  }
};

/**
 * @brief Adapter for the values tape (val tape). Fulfills InfoType.
 */
template <class TInfos, class EType>
struct ValInfo : InfoAdapterBase<ValInfo<TInfos, EType>, TInfos, double> {
  using Base = InfoAdapterBase<ValInfo<TInfos, EType>, TInfos, double>;
  using value_type = typename Base::value_type;
  using StatEntries = typename Base::StatEntries;

  static const StatEntries num = TInfos::NUM_VALUES;
  static const StatEntries fileAccess = TInfos::VAL_FILE_ACCESS;
  static const StatEntries bufferSize = TInfos::VAL_BUFFER_SIZE;
  static constexpr size_t fileIndex = 2;

  static constexpr EType error = EType::VAL_READ_FAILED;
  static constexpr size_t chunkSize = ADOLC_IO_CHUNK_SIZE / sizeof(value_type);

  static ValBuffer &getBuffer(TInfos &tapeInfos) {
    return tapeInfos.valBuffer_;
  }
  static const ValBuffer &getBuffer(const TInfos &tapeInfos) {
    return tapeInfos.valBuffer_;
  }

  static void updateBufferPositionForward(TInfos &tapeInfos) {
    getBuffer(tapeInfos).position(0);
    tapeInfos.locBuffer_.advance();
  }

  static void updateBufferPositionReverse(TInfos &tapeInfos, size_t blockSize) {
    getBuffer(tapeInfos).position(blockSize -
                                  tapeInfos.locBuffer_.retreatAndRead());
  }
};

/**
 * @brief Adapter for the Taylor tape (tay tape). Fulfills InfoTypeBase.
 */
template <class TInfos, class EType>
struct TayInfo : InfoAdapterBase<TayInfo<TInfos, EType>, TInfos, double> {
  using Self = TayInfo<TInfos, EType>;
  using Base = InfoAdapterBase<Self, TInfos, double>;
  using value_type = typename Base::value_type;
  using StatEntries = typename Base::StatEntries;
  using StatsArray = typename Base::StatsArray;

  static const StatEntries num = TInfos::NUM_TAYS;
  static const StatEntries bufferSize = TInfos::TAY_BUFFER_SIZE;
  static constexpr size_t fileIndex = 3;
  static constexpr bool removeExistingBeforeWrite = false;
  static constexpr const char *openWriteMode = "w+b";

  static constexpr EType error = EType::TAY_READ_FAILED;
  static constexpr size_t chunkSize = ADOLC_IO_CHUNK_SIZE / sizeof(value_type);

  static TayBuffer &getBuffer(TInfos &tapeInfos) {
    return tapeInfos.tayBuffer_;
  }
  static const TayBuffer &getBuffer(const TInfos &tapeInfos) {
    return tapeInfos.tayBuffer_;
  }

  static void ensureReverseReadable(TInfos &tapeInfos) {
    if (getBuffer(tapeInfos).position() == 0) {
      tapeInfos.template loadBlockIntoBufferReverse<Self>();
    }
  }

  static long reverseSeekOffset(const TInfos &tapeInfos,
                                const StatsArray &stats) {
    const auto blockSize = stats[bufferSize];
    return static_cast<long>(sizeof(value_type) *
                             (tapeInfos.nextBufferNumber * blockSize));
  }

  static void updateBufferStatsReverse(TInfos &tapeInfos,
                                       size_t /*blockSize*/) {
    tapeInfos.lastTayBlockInCore = 0;
    --tapeInfos.nextBufferNumber;
  }
};
}; // namespace ADOLC::detail

#endif // ADOLC_INFO_TYPE_H
