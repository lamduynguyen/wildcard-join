#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace utils {

struct EpochHandler {
  static constexpr uint64_t MAX_VALUE           = ~0ULL;
  static constexpr uint8_t MAX_NUMBER_OF_WORKER = 255;

  explicit EpochHandler(uint64_t no_threads);
  ~EpochHandler();

  void EpochOperation(uint64_t wid);
  void DeferFreePointer(uint64_t wid, void *ptr);

  /* Epoch-based ptr reclaimation */
  const uint64_t no_threads;
  std::atomic<uint64_t> global_epoch;
  std::vector<std::atomic<uint64_t>> local_epoch;
  std::array<std::vector<std::pair<void *, uint64_t>>, MAX_NUMBER_OF_WORKER> to_free_ptr = {};
};

class EpochGuard {
 public:
  EpochGuard(std::atomic<uint64_t> *epoch, const std::atomic<uint64_t> &global_epoch) : epoch_(epoch) {
    epoch->store(global_epoch.load());
  }

  ~EpochGuard() { epoch_->store(EpochHandler::MAX_VALUE); }

 private:
  std::atomic<uint64_t> *epoch_;
};

}  // namespace utils
