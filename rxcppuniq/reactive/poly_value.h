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

#ifndef RXCPPUNIQ_REACTIVE_POLY_VALUE_H_
#define RXCPPUNIQ_REACTIVE_POLY_VALUE_H_

#include <memory>
#include <type_traits>

#include "google/protobuf/message_lite.h"
#include "absl/container/fixed_array.h"
#include "absl/meta/type_traits.h"
#include "absl/types/any.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/reactive/object.h"
#include "rxcppuniq/reactive/protobuf.h"

namespace rx {

/**
 * Type used for representing serialized blobs of data.
 */
struct SerializedValue {
  using BytesType = absl::FixedArray<std::uint8_t>;
  std::unique_ptr<BytesType> bytes;
};

/**
 * A trait for serialization of type T.
 */
template <typename T>
struct SerializationTraits {
  static constexpr bool Available() { return false; }

  static StatusOr<SerializedValue> Serialize(T&&) {
    return RX_STATUS(UNAVAILABLE)
           << "serialization not available for this type";
  }

  static StatusOr<T> Parse(SerializedValue&&) {
    return RX_STATUS(UNAVAILABLE)
           << "serialization not available for this type";
  }
};

/**
 * NOTE: this code is currently nowhere used.
 *
 * A polymorphic representation of values which can be exchanged between
 * activities.
 *
 * Polymorphic values exist in-memory in an efficient form and can be converted
 * back to their typed equivalent. A subset of polymorphic values can also
 * be serialized over the network. This allows to build abstractions on top
 * of them which work similar on the network as in memory.
 *
 * PolyValues are not copyable but only movable. This allows them to embed
 * std::unique_ptr instances. It also reflects their purpose of being transient
 * objects passed uninterpreted through lower layers of the stack.
 *
 * Serialization (and deserialization) is performed on-demand. A value is
 * kept in its memory representation until the need to serialize actually
 * arises. In turn, a value can be constructed from a SerializedValue
 * and is not deserialized until it is actually accessed using Unpack<T>.
 */
class PolyValue : UniqueObject<absl::any> {
 private:
  template <typename T>
  using EnableIfNotCopyable =
      absl::enable_if_t<!std::is_copy_constructible<T>::value, T>;

  template <typename T>
  using EnableIfCopyable =
      absl::enable_if_t<std::is_copy_constructible<T>::value, T>;

  // A value together with a serializer. When we pack a value T where
  // SerializationTraits<T>::Available(), we wrap the value together with
  // a serialization function using ValueWithSerializer, so we are able to
  // serialize it on demand even after type information is erased.
  //
  // Note that we do not need the same treatment for deserialization.
  // Deserialization is deferred until a value of type T is actually accessed,
  // so T is known and can be used to find the serializers.
  struct ValueWithSerializer {
    absl::any value;
    std::function<StatusOr<SerializedValue>(absl::any&&)> serializer;
  };

 public:
  // Inherit constructors.
  using UniqueObject<absl::any>::UniqueObject;

  /**
   * Packs the given value into a PolyValue.
   *
   * If the value is serializable, it's serialization info will be remembered
   * if it later needs to be send over the network.
   *
   * The value can be rediscovered by poly_value.Unpack<T>.
   *
   * A value of type SerializedValue can be passed in. If this value is
   * unpacked, it will be deserialized based on the SerializationTraits<T>.
   */
  template <typename T>
  static PolyValue Pack(EnableIfCopyable<T> value) {
    auto result = PolyValue(absl::make_any<T>(std::move(value)));

    // Attach a serializer if available.
    if (SerializationTraits<T>::Available()) {
      result =
          PolyValue(absl::make_any<ValueWithSerializer>(ValueWithSerializer{
              *result.move_impl(),
              [](absl::any&& packed_value) -> StatusOr<SerializedValue> {
                if (!Is<T>(packed_value)) {
                  return RX_STATUS(StatusCode::INTERNAL)
                         << "value has not the expected type";
                }
                return SerializationTraits<T>::Serialize(
                    std::move(absl::any_cast<T>(packed_value)));
              }}));
    }
    return result;
  }

  template <typename T>
  static PolyValue Pack(EnableIfNotCopyable<T> value) {
    // std::any requires a copyable object. We work around this by wrapping
    // the value into std::shared_ptr, asserting that it is at least
    // movable.
    static_assert(std::is_move_constructible<T>::value,
                  "value must be copy or move constructible");
    auto result = PolyValue{absl::make_any<std::shared_ptr<T>>(
        std::make_shared<T>(std::move(value)))};

    // Attach a serializer if available.
    if (SerializationTraits<T>::Available()) {
      // The serializer must account for that the packed_value is actually
      // a shared_ptr to the non-copy-constructible value.
      result =
          PolyValue(absl::make_any<ValueWithSerializer>(ValueWithSerializer{
              *result.move_impl(),
              [](absl::any&& packed_value) -> StatusOr<SerializedValue> {
                if (!Is<std::shared_ptr<T>>(packed_value)) {
                  return RX_STATUS(StatusCode::INTERNAL)
                         << "value has not the expected type";
                }
                return SerializationTraits<T>::Serialize(std::move(
                    *absl::any_cast<std::shared_ptr<T>>(packed_value)));
              }}));
    }
    return result;
  }

  /**
   * Unpacks a value which was previously packed via Pack<T>(value).
   *
   * If the value is represented by SerializedValue (a byte blob), it will
   * be deserialized using SerializationTraits<T>.
   *
   * The returned status, if not ok, indicates errors like trying to unpack
   * a value which was packed using T' with T != T', as well as serialization
   * errors. In general, this method is safe modulo safety of serialization.
   *
   * Note that the PolyValue instance is consumed by this call. One consequence
   * is that it cannot be used to probe through a series of types.
   */
  template <typename T>
  StatusOr<EnableIfCopyable<T>> Unpack() && {
    auto packed_value = *move_impl();

    if (Is<std::shared_ptr<SerializedValue>>(packed_value)) {
      // Value is serialized, create it from serialized representation.
      return SerializationTraits<T>::Parse(
          std::move(*absl::any_cast<std::shared_ptr<SerializedValue>>(
              std::move(packed_value))));
    }

    if (Is<ValueWithSerializer>(packed_value)) {
      // Remove the Serializer wrapper
      packed_value =
          absl::any_cast<ValueWithSerializer>(std::move(packed_value)).value;
    }
    RX_RETURN_IF_ERROR(CheckType<T>(packed_value));
    return absl::any_cast<T>(std::move(packed_value));
  }

  template <typename T>
  StatusOr<EnableIfNotCopyable<T>> Unpack() && {
    auto packed_value = *move_impl();

    if (Is<std::shared_ptr<SerializedValue>>(packed_value)) {
      // Value is serialized, create it from serialized representation.
      return SerializationTraits<T>::Parse(
          std::move(*absl::any_cast<std::shared_ptr<SerializedValue>>(
              std::move(packed_value))));
    }

    if (Is<ValueWithSerializer>(packed_value)) {
      // Remove the Serializer wrapper
      packed_value =
          absl::any_cast<ValueWithSerializer>(std::move(packed_value)).value;
    }
    // Value is actually in a shared_ptr, move it out from there.
    RX_RETURN_IF_ERROR(CheckType<std::shared_ptr<T>>(packed_value));
    return StatusOr<T>(std::move(
        *absl::any_cast<std::shared_ptr<T>>(std::move(packed_value))));
  }

  /**
   * Serializes the PolyValue provided it is serializable, as determined
   * by SerializationTraits<T> at Pack<T> time.
   */
  StatusOr<SerializedValue> Serialize() &&;

 private:
  explicit PolyValue(absl::any value)
      : PolyValue(absl::make_unique<absl::any>(std::move(value))) {}

  // Returns status with diagnostics if value has not the expected type.
  template <typename T>
  static Status CheckType(absl::any const& value) {
    if (!Is<T>(value)) {
      return RX_STATUS(StatusCode::INVALID_ARGUMENT)
             << "value has not the requested type";
    }
    return RX_STATUS(OK);
  }

  // Test whether value has expected type.
  template <typename T>
  static bool Is(absl::any const& value) {
    return absl::any_cast<T>(&value) != nullptr;
  }
};

// Serialization traits
// ====================

template <typename T>
struct SerializationTraits<SharedMsg<T>> {
  static constexpr bool Available() { return true; }

  static StatusOr<SerializedValue> Serialize(SharedMsg<T>&& value) {
    auto msg = value.impl();
    auto bytes =
        absl::make_unique<SerializedValue::BytesType>(msg->ByteSizeLong());
    if (!msg->SerializeToArray(bytes->data(),
                               static_cast<int>(bytes->size()))) {
      return RX_STATUS(INTERNAL) << "serialization failed";
    }
    return SerializedValue{std::move(bytes)};
  }

  static StatusOr<SharedMsg<T>> Parse(SerializedValue&& serialized_value) {
    auto msg = std::make_shared<T>();
    if (!msg->ParseFromArray(
            serialized_value.bytes->data(),
            static_cast<int>(serialized_value.bytes->size()))) {
      return RX_STATUS(INTERNAL) << "deserialization failed";
    }
    return SharedMsg<T>(std::move(msg));
  }
};

template <typename T>
struct SerializationTraits<UniqueMsg<T>> {
  static constexpr bool Available() { return true; }

  static StatusOr<SerializedValue> Serialize(UniqueMsg<T>&& value) {
    auto msg = value.move_impl();
    auto bytes =
        absl::make_unique<SerializedValue::BytesType>(msg->ByteSizeLong());
    if (!msg->SerializeToArray(bytes->data(),
                               static_cast<int>(bytes->size()))) {
      return RX_STATUS(INTERNAL) << "serialization failed";
    }
    return SerializedValue{std::move(bytes)};
  }

  static StatusOr<UniqueMsg<T>> Parse(SerializedValue&& serialized_value) {
    auto msg = absl::make_unique<T>();
    if (!msg->ParseFromArray(
            serialized_value.bytes->data(),
            static_cast<int>(serialized_value.bytes->size()))) {
      return RX_STATUS(INTERNAL) << "deserialization failed";
    }
    return UniqueMsg<T>(std::move(msg));
  }
};

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_POLY_VALUE_H_
