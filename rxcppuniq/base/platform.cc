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

#include "rxcppuniq/base/platform.h"

#include <stdlib.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#endif

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "rxcppuniq/base/monitoring.h"

namespace rx {

#ifdef _WIN32
const char* kPathSeparator = "\\";
#else
const char* kPathSeparator = "/";
#endif

std::string ConcatPath(absl::string_view path1, absl::string_view path2) {
  if (path1.empty()) {
    return std::string(path2);
  }
  return absl::StrCat(path1, kPathSeparator, path2);
}

StatusOr<std::string> ReadFileToString(absl::string_view file_name) {
  auto file_name_str = std::string(file_name);
  std::ifstream is(file_name_str);
  if (!is) {
    return RX_STATUS(INTERNAL) << "cannot read file " << file_name_str;
  }
  std::ostringstream buffer;
  buffer << is.rdbuf();
  is.close();
  return buffer.str();
}

Status WriteStringToFile(absl::string_view file_name,
                         absl::string_view content) {
  auto file_name_str = std::string(file_name);
  std::ofstream os(file_name_str);
  if (!os) {
    return RX_STATUS(StatusCode::INTERNAL)
           << "cannot create file " << file_name_str;
  }
  os << content;
  os.close();
  return RX_STATUS(OK);
}

Status ReadFileToMessage(absl::string_view file_name,
                         google::protobuf::Message* message) {
  RX_CHECK(message != nullptr);
  auto file_name_str = std::string(file_name);
  std::ifstream is(file_name_str);
  if (!is) {
    return RX_STATUS(NOT_FOUND) << "cannot read file " << file_name_str;
  }
  if (!message->ParseFromIstream(&is)) {
    return RX_STATUS(INVALID_ARGUMENT) << "cannot parse message";
  }
  return RX_STATUS(OK);
}

bool FileExists(absl::string_view file_name) {
  struct stat info;
  return stat(std::string(file_name).c_str(), &info) == 0;
}

std::string MakeTempFileName() {
#ifdef __APPLE__
// Apple has marked tmpnam as deprecated. As we are compiling with -Werror,
// turning this off for this case. Apple recommends to use mkstemp instead,
// but because this opens a file, its not exactly what we want, and it's not
// portable. std::filesystem in C++17 should fix this issue.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  return tmpnam(nullptr);
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif
}

std::string BaseName(absl::string_view path) {
  // Note that find_last_of returns npos if not found, and npos+1 is guaranteed
  // to be zero.
  return std::string(path.substr(path.find_last_of(kPathSeparator) + 1));
}

Status EnsureDirExists(absl::string_view path) {
  if (FileExists(path)) {
    return RX_STATUS(OK);
  }
  auto path_str = std::string(path);
  int error;
#ifndef _WIN32
  error = mkdir(path_str.c_str(), 0733);
#else
  error = _mkdir(path_str.c_str());
#endif
  if (error) {
    return RX_STATUS(INTERNAL) << "cannot create directory " << path_str
                               << "(error code " << error << ")";
  }
  return RX_STATUS(OK);
}

Status ShellCommand(absl::string_view command, std::string* stdout_result,
                    std::string* stderr_result) {
#ifdef _WIN32
  return RX_STATUS(UNIMPLEMENTED)
         << "rx::ShellCommand not implemented for Windows";
#else
  // Prepare command for output redirection.
  std::string command_str = std::string(command);
  std::string stdout_file;
  if (stdout_result != nullptr) {
    stdout_file = MakeTempFileName();
    absl::StrAppend(&command_str, " 1>", stdout_file);
  }
  std::string stderr_file;
  if (stderr_result != nullptr) {
    stderr_file = MakeTempFileName();
    absl::StrAppend(&command_str, " 2>", stderr_file);
  }

  // Call the command.
  int result = std::system(command_str.c_str());

  // Read and remove redirected output.
  if (stdout_result != nullptr) {
    auto status_or_result = ReadFileToString(stdout_file);
    if (status_or_result.ok()) {
      *stdout_result = status_or_result.ValueOrDie();
      std::remove(stdout_file.c_str());
    } else {
      *stdout_result = "";
    }
  }
  if (stderr_result != nullptr) {
    auto status_or_result = ReadFileToString(stderr_file);
    if (status_or_result.ok()) {
      *stderr_result = status_or_result.ValueOrDie();
      std::remove(stderr_file.c_str());
    } else {
      *stderr_result = "";
    }
  }

  // Construct result.
  if (result != 0) {
    return RX_STATUS(INTERNAL) << "command execution failed: " << command_str
                               << " returns " << result;
  } else {
    return RX_STATUS(OK);
  }
#endif
}

}  // namespace rx
