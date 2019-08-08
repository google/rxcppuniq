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

#ifndef RXCPPUNIQ_BASE_PLATFORM_H_
#define RXCPPUNIQ_BASE_PLATFORM_H_

#include <string>

#include "google/protobuf/message.h"
#include "absl/strings/string_view.h"
#include "rxcppuniq/base/monitoring.h"

// This file defines platform dependent utilities.

namespace rx {

/**
 * Concatenates two file path components using platform specific separator.
 */
std::string ConcatPath(absl::string_view path1, absl::string_view path2);

/**
 * Reads file content into std::string.
 */
StatusOr<std::string> ReadFileToString(absl::string_view file_name);

/**
 * Reads file content into message.
 */
Status ReadFileToMessage(absl::string_view file_name, google::protobuf::Message* message);

/**
 * Writes std::string content into file.
 */
Status WriteStringToFile(absl::string_view file_name,
                         absl::string_view content);

/**
 * Returns the file base name of a path.
 */
std::string BaseName(absl::string_view path);

/**
 * Returns true if the file exists.
 */
bool FileExists(absl::string_view file_name);

/**
 * Makes a temporary file name.
 */
std::string MakeTempFileName();

/**
 * Ensure that the directory at the given path exists.
 */
Status EnsureDirExists(absl::string_view path);

/**
 * Calls a system (shell) command, storing stdout and stderr in the provided
 * std::string pointers if those are not null.
 */
Status ShellCommand(absl::string_view command,
                    std::string* stdout_result = nullptr,
                    std::string* stderr_result = nullptr);

}  // namespace rx

#endif  // RXCPPUNIQ_BASE_PLATFORM_H_
