
#include "rxcppuniq/reactive/reactive.h"

namespace rx {

size_t RequestBalance::kMax = std::numeric_limits<size_t>::max();

void RequestBalance::Add(size_t count) {
  balance_ = kMax - balance_ < count ? kMax : balance_ + count;
}

void RequestBalance::Remove(size_t count) {
  if (balance_ == kMax) {
    // can't remove from max.
    return;
  }
  // As long as we have not overflown into max, counting must be accurate.
  FCP_CHECK(balance_ >= count) << "negative request balance";
  balance_ -= count;
}

}  // namespace rx
