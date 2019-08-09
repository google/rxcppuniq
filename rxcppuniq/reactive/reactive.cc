/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
  RX_CHECK(balance_ >= count) << "negative request balance";
  balance_ -= count;
}

}  // namespace rx
