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

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/scheduler.h"
#include "rxcppuniq/base/testing.h"
#include "rxcppuniq/reactive/reactive_testing.h"

namespace rx {

namespace {

using std::int32_t;
using std::unique_ptr;
using std::vector;
using testing::ElementsAre;

// Generators
// ==========

TEST(ReactiveTest, GeneratorSuccess) {
  vector<int32_t> buffer;
  Publisher<int32_t>::Generator<int32_t>(
      0,
      [](int32_t& state) -> StatusOr<int32_t> {
        return state == 2 ? Done() : StatusOr<int32_t>(state++);
      })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(0, 1));
}

TEST(ReactiveTest, GeneratorSuccessWithUnique) {
  // Both generator state and element type are unique_ptr
  vector<unique_ptr<int32_t>> buffer;
  Publisher<unique_ptr<int32_t>>::Generator<unique_ptr<int32_t>>(
      absl::make_unique<int32_t>(5),
      [](unique_ptr<int32_t>& state) -> StatusOr<unique_ptr<int32_t>> {
        return *state == 6 ? Done()
                           : StatusOr<unique_ptr<int32_t>>(
                                 absl::make_unique<int32_t>((*state)++));
      })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_EQ(buffer.size(), 1);
  ASSERT_EQ(*buffer[0], 5);
}

TEST(ReactiveTest, GeneratorPropagatesError) {
  vector<int32_t> buffer;
  Publisher<int32_t>::Generator<int32_t>(
      0,
      [](int32_t& state) -> StatusOr<int32_t> {
        return state == 1 ? FCP_STATUS(INVALID_ARGUMENT)
                          : StatusOr<int32_t>(state++);
      })
      .Subscribe(BufferingSubscriber(buffer, INVALID_ARGUMENT));
  ASSERT_THAT(buffer, ElementsAre(0));
}

TEST(ReactiveTest, GeneratorValuesSuccess) {
  vector<int32_t> buffer;
  Publisher<int32_t>({1, 2, 3}).Subscribe(BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(1, 2, 3));
}

TEST(ReactiveTest, GeneratorValuesUniqueSuccess) {
  // Can't create vector with initializer_list from unique_ptr, so need
  // to do it awkwardly.
  auto values = std::vector<unique_ptr<int32_t>>();
  values.emplace_back(absl::make_unique<int32_t>(3));
  vector<unique_ptr<int32_t>> buffer;
  Publisher<unique_ptr<int32_t>>(std::move(values))
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_EQ(buffer.size(), 1);
  ASSERT_EQ(*buffer[0], 3);
}

TEST(ReactiveTest, GeneratorSubscriptionLifetime) {
  Subscription subscription;
  int32_t counter{};
  Publisher<int32_t>({1, 2, 3}).Subscribe(
      [&subscription](Subscription const& s) {
        subscription = s;
        s.Request(1);
      },
      [&counter](Subscription const& s, StatusOr<int32_t> event) {
        if (!IsEnd(event)) {
          counter++;
          s.Request(1);
        }
      });
  ASSERT_EQ(counter, 3);
  ASSERT_TRUE(subscription.has_ended());
}

// Unique usage of Publisher
// =========================

TEST(ReactiveTest, NonUniqueUseFailure) {
  vector<int32_t> buffer;
  auto literals = Publisher<int32_t>({1, 2, 3});
  std::move(literals).Subscribe(BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(1, 2, 3));
  buffer.clear();
  ASSERT_DEATH(
      {
        std::move(literals).Subscribe(BufferingSubscriber(buffer));  // NOLINT
      },
      "implementation missing \\(maybe moved\\)");
}

// Map Operator
// ============

TEST(ReactiveTest, MapSuccess) {
  vector<int32_t> buffer;
  Publisher<int32_t>({1, 2, 3})
      .Map<int32_t>([](int32_t x) { return x + 1; })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(2, 3, 4));
}

TEST(ReactiveTest, MapUniqueSuccess) {
  vector<std::unique_ptr<int32_t>> buffer;
  auto values = std::vector<unique_ptr<int32_t>>();
  values.emplace_back(absl::make_unique<int32_t>(3));
  Publisher<unique_ptr<int32_t>>(std::move(values))
      .Map<unique_ptr<int32_t>>([](unique_ptr<int32_t> x) {
        return absl::make_unique<int32_t>(*x + 1);
      })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_EQ(buffer.size(), 1);
  ASSERT_EQ(*buffer[0], 4);
}

TEST(ReactiveTest, MapPropagatesError) {
  Publisher<int32_t>::Error(FCP_STATUS(INVALID_ARGUMENT))
      .Map<int32_t>([](int32_t x) { return x + 1; })
      .Subscribe(ExpectErrorSubscriber<int32_t>(INVALID_ARGUMENT));
}

TEST(ReactiveTest, MapSubscriptionLifetime) {
  Subscription subscription;
  int32_t counter{};
  Publisher<int32_t>({1, 2, 3})
      .Map<int32_t>([](int32_t x) { return x + 1; })
      .Subscribe(
          [&subscription](Subscription const& s) {
            subscription = s;
            s.Request(1);
          },
          [&counter](Subscription const& s, StatusOr<int32_t> event) {
            if (!IsEnd(event)) {
              counter++;
              s.Request(1);
            }
          });
  ASSERT_EQ(counter, 3);
  ASSERT_TRUE(subscription.has_ended());
}

// Buffer Operator
// ===============

// The Buffer operator is based on the some implementation than Map, so we do
// not need to test all variations.

TEST(ReactiveTest, BufferSuccess) {
  vector<vector<int32_t>> buffer;
  Publisher<int32_t>({0, 1, 2}).Buffer(2).Subscribe(
      BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(ElementsAre(0, 1), ElementsAre(2)));
}

// FlatMap Operator
// ================

TEST(ReactiveTest, FlatMapSuccess) {
  vector<int64_t> buffer;
  Publisher<int32_t>({1, 2, 3})
      .FlatMap<int64_t>([](int32_t x) {
        return Publisher<int64_t>({x, x});
      })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(1, 1, 2, 2, 3, 3));
}

TEST(ReactiveTest, FlatMapUniqueSuccess) {
  vector<unique_ptr<int64_t>> buffer;
  auto values = std::vector<unique_ptr<int32_t>>();
  values.emplace_back(absl::make_unique<int32_t>(1));
  Publisher<unique_ptr<int32_t>>(std::move(values))
      .FlatMap<unique_ptr<int64_t>>([](unique_ptr<int32_t> x) {
        auto values = std::vector<unique_ptr<int64_t>>();
        values.emplace_back(absl::make_unique<int64_t>(*x + 1));
        values.emplace_back(absl::make_unique<int64_t>(*x + 2));
        return Publisher<unique_ptr<int64_t>>(std::move(values));
      })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_EQ(buffer.size(), 2);
  ASSERT_EQ(*buffer[0], 2);
  ASSERT_EQ(*buffer[1], 3);
}

TEST(ReactiveTest, FlatMapSuccessWithEmpty) {
  vector<int32_t> buffer;
  Publisher<int32_t>({1, 2, 3})
      .FlatMap<int32_t>([](int32_t x) {
        if (x % 2 == 0) {
          return Publisher<int32_t>({x, x});
        } else {
          return Publisher<int32_t>{};
        }
      })
      .Subscribe(BufferingSubscriber(buffer));
  ASSERT_THAT(buffer, ElementsAre(2, 2));
}

TEST(ReactiveTest, FlatMapErrorHandling) {
  vector<int32_t> buffer;
  Publisher<int32_t>({1, 2, 3})
      .FlatMap<int32_t>([](int32_t x) {
        if (x != 2) {
          return Publisher<int32_t>({x, x});
        } else {
          return Publisher<int32_t>::Error(FCP_STATUS(NOT_FOUND));
        }
      })
      .Subscribe(BufferingSubscriber(buffer, NOT_FOUND));
  // We expect that stream stopped producing after the first element and
  // returns NOT_FOUND as last element (checked by BufferingSubscriber above).
  ASSERT_THAT(buffer, ElementsAre(1, 1));
}

// Async Operator

// TODO(wrwg): add more tests to ensure concurreny is working as expected
//   (e.g. code is executed by the right worker).

TEST(ReactiveTest, AsyncSuccess) {
  auto scheduler = CreateThreadPoolScheduler(2);
  auto produce_on = SharedObject<Worker>(scheduler->CreateWorker());
  auto consume_on = SharedObject<Worker>(scheduler->CreateWorker());
  vector<int64_t> buffer;
  Publisher<int64_t>({1, 2, 3})
      .Async(produce_on, consume_on)
      .Subscribe(BufferingSubscriber(buffer));
  scheduler->WaitUntilIdle();
  ASSERT_THAT(buffer, ElementsAre(1, 2, 3));
}

TEST(ReactiveTest, AsyncError) {
  auto scheduler = CreateThreadPoolScheduler(2);
  auto produce_on = SharedObject<Worker>(scheduler->CreateWorker());
  auto consume_on = SharedObject<Worker>(scheduler->CreateWorker());
  vector<int64_t> buffer;
  Publisher<int64_t>::Error(FCP_STATUS(NOT_FOUND))
      .Async(produce_on, consume_on)
      .Subscribe(BufferingSubscriber(buffer, NOT_FOUND));
  scheduler->WaitUntilIdle();
  // We expect that stream stopped produced no elements and
  // returns NOT_FOUND as last element (checked by BufferingSubscriber above).
  ASSERT_THAT(buffer, ElementsAre());
}

}  // namespace

}  // namespace rx
