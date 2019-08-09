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

#include <stdio.h>
#include <stdlib.h>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/platform.h"

namespace rx {

const char* kProjectRoot = "";

std::string TestName() {
  auto test_info = testing::UnitTest::GetInstance()->current_test_info();
  return absl::StrReplaceAll(test_info->name(), {{"/", "_"}});
}

std::string TestCaseName() {
  auto test_info = testing::UnitTest::GetInstance()->current_test_info();
  return absl::StrReplaceAll(test_info->test_case_name(), {{"/", "_"}});
}

std::string GetTestDataPath(absl::string_view relative_path) {
  return ConcatPath(kProjectRoot, relative_path);
}

std::string TemporaryTestFile(absl::string_view suffix) {
  return ConcatPath(testing::TempDir(), absl::StrCat(TestName(), suffix));
}

bool ShouldUpdateBaseline() {
  return getenv("RX_UPDATE_BASELINE");
}

StatusOr<std::string> ComputeDiff(absl::string_view baseline_file,
                                  absl::string_view content) {
  std::string diff_result;
  std::string baseline_file_str = GetTestDataPath(baseline_file);
  if (!FileExists(baseline_file_str)) {
    diff_result = absl::StrCat("no recorded baseline file ", baseline_file_str);
  } else {
#ifndef _WIN32
    // Expect Unix diff command to be available.
    auto provided_file = TemporaryTestFile(".provided");
    auto status = WriteStringToFile(provided_file, content);
    if (status.code() != OK) {
      return status;
    }
    std::string std_out, std_err;
    status = ShellCommand(
        absl::StrCat("diff -u ", baseline_file_str, " ", provided_file),
        &std_out, &std_err);
    std::remove(provided_file.c_str());
    if (status.code() != OK) {
      if (!std_err.empty()) {
        // Indicates a failure in diff execution itself.
        return RX_STATUS(INTERNAL) << "command failed: " << std_err;
      }
      diff_result = std_out;
    }
#else  // _WIN32
    // For now we do a simple std::string compare on Windows.
    auto status_or_string = ReadFileToString(baseline_file_str);
    if (!status_or_string.ok()) {
      return status_or_string.status();
    }
    if (status_or_string.ValueOrDie() != content) {
      diff_result = "baseline and actual differ (see respective files)";
    }
#endif
  }
  return diff_result;
}

StatusOr<std::string> VerifyAgainstBaseline(absl::string_view baseline_file,
                                            absl::string_view content) {
  auto status_or_diff_result = ComputeDiff(baseline_file, content);
  if (!status_or_diff_result.ok()) {
    return status_or_diff_result;
  }
  auto& diff_result = status_or_diff_result.ValueOrDie();
  if (diff_result.empty()) {
    // success
    return status_or_diff_result;
  }

  // Determine the location where to store the new baseline.
  std::string new_baseline_file;
  bool auto_update = false;

  if (new_baseline_file.empty() && ShouldUpdateBaseline()) {
    new_baseline_file = GetTestDataPath(baseline_file);
    diff_result =
        absl::StrCat("\nAutomatically updated baseline file: ", baseline_file);
    auto_update = true;
  }

  if (new_baseline_file.empty()) {
    // Store new baseline file in a TMP location.
#ifndef _WIN32
    const char* temp_dir = "/tmp";
#else
    const char* temp_dir = getenv("TEMP");
#endif
    auto temp_output_dir =
        ConcatPath(temp_dir, absl::StrCat("rx_", TestCaseName()));
    EnsureDirExists(temp_output_dir);
    new_baseline_file = ConcatPath(temp_output_dir, BaseName(baseline_file));
    absl::StrAppend(&diff_result, "\nNew baseline file: ", new_baseline_file);
    absl::StrAppend(&diff_result, "\nTo update, use:");
    absl::StrAppend(&diff_result, "\n\n cp ", new_baseline_file, " ",
                    baseline_file, "\n");
  }

  if (!auto_update) {
    absl::StrAppend(&diff_result,
                    "\nTo automatically update baseline files, use");
    absl::StrAppend(&diff_result, "\nenvironment variable RX_UPDATE_BASELINE.");
  }

  // Write the new baseline.
  auto status = WriteStringToFile(new_baseline_file, content);
  if (status.code() != OK) {
    return status;
  }

  // Deliver result.
  if (auto_update) {
    RX_LOG(INFO) << diff_result;
    diff_result = "";  // make test pass
  }
  return diff_result;
}

StatusMatcher IsCode(StatusCode code) { return StatusMatcher(code); }
StatusMatcher IsOk() { return IsCode(OK); }

}  // namespace rx
