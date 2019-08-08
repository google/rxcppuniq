/*
 * Copyright 2017 Google LLC
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

#include "rxcppuniq/base/testing.h"

#include "gtest/gtest.h"
#include "rxcppuniq/base/monitoring.h"

namespace rx {

namespace {

TEST(TestingTest, TestName) { ASSERT_EQ(TestName(), "TestName"); }

TEST(TestingTest, TestDataPath) {
  auto path =
      GetTestDataPath("rxcppuniq/base/testdata/verify_baseline_test.baseline");
  ASSERT_TRUE(FileExists(path));
}

TEST(TestingTest, TemporaryTestFile) {
  auto path = TemporaryTestFile(".dat");
  ASSERT_EQ(WriteStringToFile(path, "test").code(), OK);
  ASSERT_EQ(ReadFileToString(path).ValueOrDie(), "test");
}

TEST(TestingTest, VerifyAgainstBaseline) {
  auto status_or_diff = VerifyAgainstBaseline(
      "rxcppuniq/base/testdata/verify_baseline_test.baseline",
      "Dies ist ein Test.");
  ASSERT_TRUE(status_or_diff.ok())
      << status_or_diff.status().ShortDebugString();
  if (!status_or_diff.ValueOrDie().empty()) {
    FAIL() << status_or_diff.ValueOrDie();
  }
}

TEST(TestingTest, VerifyAgainstBaselineFailure) {
  auto status_or_diff = VerifyAgainstBaseline(
      "rxcppuniq/base/testdata/verify_baseline_test.baseline",
      "Dies ist kein Test.");
  ASSERT_TRUE(status_or_diff.ok())
      << status_or_diff.status().ShortDebugString();
  // The actual output of the diff is much dependent on which mode we run
  // in and on which platform. Hence only test whether *some* thing is reported.
  ASSERT_FALSE(status_or_diff.ValueOrDie().empty());
}

}  // namespace

}  // namespace rx
