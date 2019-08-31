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

#include "gtest/gtest.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/testing.h"

namespace rx {

namespace {

TEST(PlatformTest, ConcatPath) {
  auto combined = ConcatPath("first", "second");
#if _WIN32
  ASSERT_EQ(combined, "first\\second");
#else
  ASSERT_EQ(combined, "first/second");
#endif
}

TEST(PlatformTest, ReadWriteString) {
  auto file = TemporaryTestFile(".dat");
  ASSERT_EQ(WriteStringToFile(file, "Ein Text").code(), OK);
  auto status_or_string = ReadFileToString(file);
  ASSERT_TRUE(status_or_string.ok())
      << status_or_string.status().ShortDebugString();
  ASSERT_EQ(status_or_string.ValueOrDie(), "Ein Text");
}

TEST(PlatformTest, ReadStringFails) {
  ASSERT_FALSE(ReadFileToString("foobarbaz").ok());
}

TEST(PlatformTest, BaseName) {
  ASSERT_EQ(BaseName(ConcatPath("foo", "bar.x")), "bar.x");
}

TEST(PlatformTest, FileExists) {
  auto file = TemporaryTestFile(".dat");
  ASSERT_EQ(WriteStringToFile(file, "Ein Text").code(), OK);
  ASSERT_TRUE(FileExists(file));
}

TEST(PlatformTest, FileExistsNot) { ASSERT_FALSE(FileExists("foobarbaz")); }

TEST(PlatformTest, MakeTempFileName) {
  auto file = MakeTempFileName();
  ASSERT_EQ(WriteStringToFile(file, "Ein Text").code(), OK);
  ASSERT_TRUE(FileExists(file));
}

TEST(PlatformTest, EnsureDirExists) {
  auto dir = TemporaryTestFile("dir");
  EXPECT_EQ(EnsureDirExists(dir).code(), OK);
  EXPECT_EQ(EnsureDirExists(dir).code(), OK);
  auto file_in_dir = ConcatPath(dir, "x.dat");
  ASSERT_EQ(WriteStringToFile(file_in_dir, "Ein Text").code(), OK);
  EXPECT_TRUE(FileExists(file_in_dir));
}

TEST(PlatformTest, ShellCommand) {
#ifndef _WIN32
  std::string std_out, std_err;
  ASSERT_EQ(ShellCommand("ls", &std_out, &std_err).code(), OK);
  EXPECT_FALSE(std_out.empty());
  EXPECT_TRUE(std_err.empty());
#else
  ASSERT_EQ(ShellCommand("any").code(), UNIMPLEMENTED);
#endif
}

}  // namespace

}  // namespace rx
