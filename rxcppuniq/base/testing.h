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

#ifndef RXCPPUNIQ_BASE_TESTING_H_
#define RXCPPUNIQ_BASE_TESTING_H_

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/platform.h"

// This file defines platform dependent utilities for testing,
// based on the public version of googletest.

namespace rx {

/** Returns the current test's name. */
std::string TestName();

/**
 * Gets path to a test data file based on a path relative to FCP project root.
 */
std::string GetTestDataPath(absl::string_view relative_path);

/**
 * Creates a temporary file name with given suffix unique for the running test.
 */
std::string TemporaryTestFile(absl::string_view suffix);

/**
 * Verifies a provided content against an expected stored in a baseline file.
 * Returns an empty std::string if both are identical, otherwise a diagnostic
 * message for error reports.
 *
 * A return status of not ok indicates an operational error which made the
 * comparison impossible.
 *
 * The baseline file name must be provided relative to the project root.
 */
StatusOr<std::string> VerifyAgainstBaseline(absl::string_view baseline_file,
                                            absl::string_view content);

/**
 * Polymorphic matchers for Status or StatusOr on status code.
 */
template <typename T>
bool IsCode(StatusOr<T> const& x, StatusCode code) {
  return x.status().code() == code;
}
inline bool IsCode(Status const& x, StatusCode code) {
  return x.code() == code;
}

template <typename T>
class StatusMatcherImpl : public ::testing::MatcherInterface<T> {
 public:
  StatusMatcherImpl(StatusCode code) : code_(code) {}
  void DescribeTo(::std::ostream* os) const override {
    *os << "is " << StatusCode_Name(code_);
  }
  void DescribeNegationTo(::std::ostream* os) const override {
    *os << "is not " << StatusCode_Name(code_);
  }
  bool MatchAndExplain(
      T x, ::testing::MatchResultListener* listener) const override {
    return IsCode(x, code_);
  }

 private:
  StatusCode code_;
};

class StatusMatcher {
 public:
  StatusMatcher(StatusCode code) : code_(code) {}
  template <typename T>
  operator testing::Matcher<T>() const {
    return ::testing::MakeMatcher(new StatusMatcherImpl<T>(code_));
  }

 private:
  StatusCode code_;
};

StatusMatcher IsCode(StatusCode code);
StatusMatcher IsOk();

}  // namespace rx

#endif  // RXCPPUNIQ_BASE_TESTING_H_
