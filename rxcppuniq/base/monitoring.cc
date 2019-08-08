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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> /* for abort() */
#include <string.h>

#include <cstring>

#include "absl/strings/str_cat.h"
#include "rxcppuniq/base/platform.h"

namespace rx {

namespace internal {

Logger* logger = new Logger();

const char* LogSeverityString(absl::LogSeverity severity) {
  switch (severity) {
    case absl::LogSeverity::kInfo:
      return "I";
    case absl::LogSeverity::kWarning:
      return "W";
    case absl::LogSeverity::kError:
      return "E";
    case absl::LogSeverity::kFatal:
      return "F";
    default:
      return "U";
  }
}

void Logger::Log(const char* file, int line, absl::LogSeverity severity,
                 const char* message) {
  auto base_file_name = BaseName(file);
  fprintf(stderr, "%c %s:%d %s\n", absl::LogSeverityName(severity)[0],
          base_file_name.c_str(), line, message);
}

StatusBuilder::StatusBuilder(StatusCode code, const char* file, int line)
    : file_(file), line_(line), code_(code), message_() {}

StatusBuilder::StatusBuilder(StatusBuilder const& other)
    : file_(other.file_),
      line_(other.line_),
      code_(other.code_),
      message_(other.message_.str()) {}

StatusBuilder::operator Status() {
  Status result;
  result.set_code(code_);
  const auto message_str = message_.str();
  if (code_ != OK) {
    result.set_message(absl::StrCat("(at ", BaseName(file_), ":",
                                    std::to_string(line_), ") ", message_str));
    if (log_severity_ != kNoLog) {
      logger->Log(file_, line_, log_severity_,
                  absl::StrCat("[", code_, "] ", message_str).c_str());
      if (log_severity_ == absl::LogSeverity::kFatal) {
        abort();
      }
    }
  }
  return result;
}

const Status* kOkStatus = new Status();
Status const* OkStatus() { return kOkStatus; }

}  // namespace internal

}  // namespace rx
