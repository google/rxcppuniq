/*
 * Copyright 2019 Google LLC
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

#ifndef RXCPPUNIQ_BASE_UNIQUE_VALUE_H_
#define RXCPPUNIQ_BASE_UNIQUE_VALUE_H_

#include <utility>
#include "absl/types/optional.h"

namespace rx {

/**
 * UniqueValue<T> provides move-only semantics for some value of type T.
 *
 * Its semantics are much like std::unique_ptr, but without requiring an
 * allocation and pointer indirection (recall a moved-from std::unique_ptr is
 * reset to nullptr).
 *
 * Instead, UniqueValue is represented just like absl::optional - but
 * has_value() == false once moved-from. absl::optional does *not* reset when
 * moved from (even if the wrapped type is move-only); that's consistent, but
 * not especially desirable.
 *
 * Since UniqueValue is always move-only, including a UniqueValue member is
 * sufficient for a containing aggregate to be move-only.
 */
template <typename T>
class UniqueValue {
 public:
  constexpr explicit UniqueValue(T val) : value_(std::move(val)) {}

  UniqueValue(UniqueValue const&) = delete;
  UniqueValue(UniqueValue&& other) : value_(std::move(other.value_)) {
    other.value_.reset();
  }

  UniqueValue& operator=(UniqueValue other) {
    value_.swap(other.value_);
    return *this;
  }

  /**
   * Indicates if this instance holds a value (i.e. has not been moved away).
   *
   * It is an error to dereference this UniqueValue if !has_value().
   */
  constexpr bool has_value() const {
    return value_.has_value();
  }

  constexpr T const& operator*() const & {
    return *value_;
  }

  T& operator*() & {
    return *value_;
  }

  T const* operator->() const {
    return &*value_;
  }

  T* operator->() {
    return &*value_;
  }

 private:
  absl::optional<T> value_;
};

/**
 * Makes a UniqueValue<T> given constructor arguments for T
 * (like absl::make_unique).
 */
template <typename T, typename... Args>
constexpr UniqueValue<T> MakeUniqueValue(Args&&... args) {
  return UniqueValue<T>(T(std::forward<Args>(args)...));
}

}  // namespace rx

#endif  // RXCPPUNIQ_BASE_UNIQUE_VALUE_H_
