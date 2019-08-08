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

#include "rxcppuniq/base/monitoring.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace rx {

namespace {

TEST(MonitoringTest, LogInfo) {
  testing::internal::CaptureStderr();
  FCP_LOG(INFO) << "info log of something happening";
  std::string output = testing::internal::GetCapturedStderr();
  ASSERT_THAT(output,
              testing::MatchesRegex("I.*info log of something happening\n"));
}

TEST(MonitoringTest, LogWarning) {
  testing::internal::CaptureStderr();
  FCP_LOG(WARNING) << "warning log of something happening";
  std::string output = testing::internal::GetCapturedStderr();
  ASSERT_THAT(output,
              testing::MatchesRegex("W.*warning log of something happening\n"));
}

TEST(MonitoringTest, LogError) {
  testing::internal::CaptureStderr();
  FCP_LOG(ERROR) << "error log of something happening";
  std::string output = testing::internal::GetCapturedStderr();
  ASSERT_THAT(output,
              testing::MatchesRegex("E.*error log of something happening\n"));
}

TEST(MonitoringTest, LogFatal) {
  ASSERT_DEATH({ FCP_LOG(FATAL) << "fatal log"; }, "fatal log");
}

TEST(MonitoringTest, LogIfTrue) {
  testing::internal::CaptureStderr();
  FCP_LOG_IF(INFO, true) << "some log";
  std::string output = testing::internal::GetCapturedStderr();
  ASSERT_THAT(output, testing::MatchesRegex("I.*some log\n"));
}

TEST(MonitoringTest, LogIfFalse) {
  testing::internal::CaptureStderr();
  FCP_LOG_IF(INFO, false) << "some log";
  std::string output = testing::internal::GetCapturedStderr();
  ASSERT_EQ(output, "");
}

TEST(MonitoringTest, CheckSucceeds) { FCP_CHECK(1 < 2); }

TEST(MonitoringTest, CheckFails) {
  ASSERT_DEATH({ FCP_CHECK(1 < 0); }, "Check failed: 1 < 0.");
}

TEST(MonitoringTest, StatusOr) {
  StatusOr<int> ok_status(1);
  ASSERT_TRUE(ok_status.ok());
  ASSERT_EQ(ok_status.ValueOrDie(), 1);

  StatusOr<int> fail_status = FCP_STATUS(ABORTED) << "operation aborted";
  ASSERT_FALSE(fail_status.ok());
  ASSERT_EQ(fail_status.status().code(), ABORTED);
  ASSERT_THAT(fail_status.status().message(),
              testing::MatchesRegex(".*operation aborted"));
  ASSERT_DEATH({ fail_status.ValueOrDie(); },
               "Check failed: ok\\(\\). StatusOr has no value");
}

}  // namespace
}  // namespace rx
