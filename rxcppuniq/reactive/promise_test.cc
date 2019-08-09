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

#include "rxcppuniq/reactive/promise.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/testing.h"

namespace rx {
namespace {

TEST(Promise, Fulfill) {
  Promise<int> p{22};
  std::move(p).Subscribe([](StatusOr<int> result) {
    EXPECT_THAT(result, IsOk());
    EXPECT_EQ(result.ValueOrDie(), 22);
  });
}

TEST(Promise, Error) {
  Promise<int> p = Promise<int>::Error(RX_STATUS(INVALID_ARGUMENT));
  std::move(p).Subscribe([](StatusOr<int> result) {
    EXPECT_THAT(result.status(), IsCode(INVALID_ARGUMENT));
  });
}

TEST(Promise, ContinueWith) {
  Promise<int> p{1};
  std::move(p)
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .Subscribe([](StatusOr<int> result) {
        EXPECT_THAT(result, IsOk());
        EXPECT_EQ(result.ValueOrDie(), 3);
      });
}

TEST(Promise, ContinueWithErrors) {
  Promise<int> p{1};
  std::move(p)
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .ContinueWith<int>(
          [](int x) { return Promise<int>::Error(RX_STATUS(INTERNAL)); })
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .Subscribe(
          [](StatusOr<int> result) { EXPECT_THAT(result, IsCode(INTERNAL)); });
}

}  // namespace
}  // namespace rx
