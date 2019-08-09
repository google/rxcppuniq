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

#include <list>
#include <queue>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/testing.h"
#include "rxcppuniq/reactive/protobuf.h"
#include "rxcppuniq/reactive/reactive.h"
#include "rxcppuniq/reactive/testdata/test.pb.h"

namespace rx {

using absl::make_unique;
using std::int64_t;
using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;

namespace {

TEST(PolyValueTest, Primitives) {
  EXPECT_TRUE(PolyValue::Pack<bool>(true).Unpack<bool>().ValueOrDie());
  EXPECT_EQ(PolyValue::Pack<int64_t>(42).Unpack<int64_t>().ValueOrDie(), 42);
  EXPECT_THAT(PolyValue::Pack<int64_t>(42).Unpack<bool>(),
              IsCode(StatusCode::INVALID_ARGUMENT));
}

TEST(PolyValueTest, SharedPtr) {
  auto shared = make_shared<int64_t>(84);
  auto weak = std::weak_ptr<int64_t>(shared);  // for control
  PolyValue value = PolyValue::Pack<shared_ptr<int64_t>>(std::move(shared));
  EXPECT_EQ(*std::move(value).Unpack<shared_ptr<int64_t>>().ValueOrDie(), 84);
  EXPECT_TRUE(weak.expired());
}

TEST(PolyValueTest, UniquePtr) {
  auto unique = make_unique<int64_t>(48);
  PolyValue value = PolyValue::Pack<unique_ptr<int64_t>>(std::move(unique));
  EXPECT_EQ(*std::move(value).Unpack<unique_ptr<int64_t>>().ValueOrDie(), 48);
}

TEST(PolyValueTest, UniqueMsg) {
  auto msg = UniqueMsg<TestMessage>();
  msg->set_value(42);
  auto value = PolyValue::Pack<UniqueMsg<TestMessage>>(std::move(msg));
  msg = std::move(value).Unpack<UniqueMsg<TestMessage>>().ValueOrDie();
  EXPECT_EQ(msg->value(), 42);
}

TEST(PolyValueTest, UniqueMsgSerialization) {
  auto msg = UniqueMsg<TestMessage>();
  msg->set_value(42);
  auto value = PolyValue::Pack<UniqueMsg<TestMessage>>(std::move(msg));
  auto serialized_value = std::move(value).Serialize().ValueOrDie();
  value = PolyValue::Pack<SerializedValue>(std::move(serialized_value));
  msg = std::move(value).Unpack<UniqueMsg<TestMessage>>().ValueOrDie();
  EXPECT_EQ(msg->value(), 42);
}

TEST(PolyValueTest, SharedMsgSerialization) {
  auto msg = SharedMsg<TestMessage>();
  msg->set_value(42);
  auto value = PolyValue::Pack<SharedMsg<TestMessage>>(std::move(msg));
  auto serialized_value = std::move(value).Serialize().ValueOrDie();
  value = PolyValue::Pack<SerializedValue>(std::move(serialized_value));
  msg = std::move(value).Unpack<SharedMsg<TestMessage>>().ValueOrDie();
  EXPECT_EQ(msg->value(), 42);
}

TEST(PolyValueTest, UniqueMsgParseError) {
  auto serialized_value =
      SerializedValue{absl::make_unique<SerializedValue::BytesType>(4)};
  (*serialized_value.bytes)[0] = 'c';
  (*serialized_value.bytes)[1] = 'r';
  (*serialized_value.bytes)[2] = 'a';
  (*serialized_value.bytes)[3] = 'p';
  auto value = PolyValue::Pack<SerializedValue>(std::move(serialized_value));
  auto msg = std::move(value).Unpack<UniqueMsg<TestMessage>>();
  EXPECT_THAT(msg.status(), IsCode(INTERNAL));
}

}  // namespace
}  // namespace rx
