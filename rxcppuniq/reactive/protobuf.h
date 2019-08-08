#ifndef RXCPPUNIQ_REACTIVE_PROTOBUF_H_
#define RXCPPUNIQ_REACTIVE_PROTOBUF_H_

// Protocol Buffer Integration (Messages only)

#include <memory>

#include "google/protobuf/message_lite.h"
#include "absl/memory/memory.h"
#include "rxcppuniq/reactive/object.h"

namespace rx {

/**
 * Defines a UniqueObject wrapper around a proto message.
 */
template <typename T>
class UniqueMsg : public UniqueObject<T> {
  static_assert(std::is_base_of<google::protobuf::MessageLite, T>::value,
                "expected T is google::protobuf::MessageLite for UniqueMsg<T>");

 public:
  // Inherit constructors.
  using UniqueObject<T>::UniqueObject;

  // Default constructor creates a new message of this type.
  UniqueMsg() : UniqueMsg(absl::make_unique<T>()) {}
};

/**
 * Defines a SharedObject wrapper around a proto message.
 */
template <typename T>
class SharedMsg : public SharedObject<T> {
  static_assert(std::is_base_of<google::protobuf::MessageLite, T>::value,
                "expected T is google::protobuf::MessageLite for SharedMsg<T>");

 public:
  // Inherit constructors.
  using SharedObject<T>::SharedObject;

  // Default constructor creates a new message of this type.
  SharedMsg() : SharedMsg(std::make_shared<T>()) {}
};

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_PROTOBUF_H_
