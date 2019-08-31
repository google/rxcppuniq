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

#include "rxcppuniq/reactive/poly_value.h"

namespace rx {

StatusOr<SerializedValue> PolyValue::Serialize() && {
  auto packed_value = *move_impl();
  if (packed_value.type() != typeid(ValueWithSerializer)) {
    return RX_STATUS(StatusCode::INVALID_ARGUMENT)
           << "value is not serializable";
  }
  auto value_with_serializer =
      absl::any_cast<ValueWithSerializer>(std::move(packed_value));
  return value_with_serializer.serializer(
      std::move(value_with_serializer.value));
}

}  // namespace rx
