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
