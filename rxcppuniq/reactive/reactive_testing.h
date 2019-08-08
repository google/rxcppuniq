#ifndef RXCPPUNIQ_REACTIVE_REACTIVE_TESTING_H_
#define RXCPPUNIQ_REACTIVE_REACTIVE_TESTING_H_

// Helpers for testing reactive streams.

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/testing.h"
#include "rxcppuniq/reactive/reactive.h"

namespace rx {

/**
 * A test helper which creates a subscriber which buffers the elements
 * received.
 */
template <typename T>
std::unique_ptr<Subscriber<T>> BufferingSubscriber(
    std::vector<T>& buffer, StatusCode end_code = OUT_OF_RANGE) {
  return Subscriber<T>::MakeWithFlowControl(
      [end_code, &buffer](StatusOr<T> event) {
        if (event.ok()) {
          buffer.emplace_back(std::move(event.ValueOrDie()));
        } else if (end_code != OK) {
          EXPECT_THAT(event.status(), IsCode(end_code));
        }
      });
}

/**
 * A test helper which creates a subscriber which expects a specific error code.
 */
template <typename T>
std::unique_ptr<Subscriber<T>> ExpectErrorSubscriber(StatusCode code) {
  return Subscriber<T>::MakeWithFlowControl(
      [code](StatusOr<T> event) { EXPECT_EQ(event.status().code(), code); });
}

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_REACTIVE_TESTING_H_
