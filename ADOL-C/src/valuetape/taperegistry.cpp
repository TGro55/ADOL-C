#include <adolc/adolcexport.h>
#include <adolc/tape_interface.h>
#include <adolc/valuetape/taperegistry.h>
#include <adolc/valuetape/valuetape.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <vector>

namespace {

struct TapeRegistry {
  std::shared_mutex mutex;
  std::vector<std::unique_ptr<ValueTape>> tapes;
  int nextTapeId{0};
};

TapeRegistry &registry() {
  static TapeRegistry tapeRegistry;
  return tapeRegistry;
}

} // namespace

ADOLC_API std::vector<std::unique_ptr<ValueTape>> &tapeBuffer() {
  return registry().tapes;
}

ADOLC_API ValueTape *findTapePtr_(short tapeId) {
  std::shared_lock lock(registry().mutex);
  auto tape_iter =
      std::find_if(tapeBuffer().begin(), tapeBuffer().end(),
                   [&tapeId](auto &&tape) { return tape->tapeId() == tapeId; });
  return (tape_iter != tapeBuffer().end()) ? tape_iter->get() : nullptr;
}

ADOLC_API ValueTape *findTapePtr(short tapeId) {
  ValueTape *tape = findTapePtr_(tapeId);
  if (!tape)
    ADOLCError::fail(ADOLCError::ErrorType::NO_TAPE_ID, CURRENT_LOCATION,
                     ADOLCError::FailInfo{.info1 = tapeId});
  return tape;
}

ADOLC_API ValueTape &findTape(short tapeId) { return *findTapePtr(tapeId); }

ADOLC_API void setCurrentTape(short tapeId) {
  currentTapePtr() = findTapePtr(tapeId);
}

ADOLC_API short createNewTape() {
  std::unique_lock lock(registry().mutex);
  if (registry().nextTapeId > std::numeric_limits<short>::max())
    throw std::overflow_error("No unused tape IDs remain");

  const short tapeId = static_cast<short>(registry().nextTapeId);
  auto tape = std::make_unique<ValueTape>(tapeId);
  ValueTape *const tapePtr = tape.get();
  tapeBuffer().emplace_back(std::move(tape));
  ++registry().nextTapeId;

  if (currentTapePtr() == nullptr)
    setCurrentTape(tapePtr);
  return tapeId;
}

ADOLC_API void cachedTraceTags(std::vector<short> &result) {
  std::shared_lock lock(registry().mutex);
  result.clear();
  result.reserve(tapeBuffer().size());
  for (const auto &tape : tapeBuffer())
    result.push_back(tape->tapeId());
}
