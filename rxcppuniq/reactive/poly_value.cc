#include "rxcppuniq/reactive/poly_value.h"

namespace rx {

StatusOr<SerializedValue> PolyValue::Serialize() && {
  auto packed_value = *move_impl();
  if (packed_value.type() != typeid(ValueWithSerializer)) {
    return FCP_STATUS(StatusCode::INVALID_ARGUMENT)
           << "value is not serializable";
  }
  auto value_with_serializer =
      absl::any_cast<ValueWithSerializer>(std::move(packed_value));
  return value_with_serializer.serializer(
      std::move(value_with_serializer.value));
}

}  // namespace rx
