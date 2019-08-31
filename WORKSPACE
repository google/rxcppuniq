# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

workspace(name = "rxcppuniq")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# bazel-skylib 0.8.0 released 2019.03.20
# https://github.com/bazelbuild/bazel-skylib/releases/tag/0.8.0
http_archive(
    name = "bazel_skylib",
    url = "https://github.com/bazelbuild/bazel-skylib/releases/download/0.8.0/bazel-skylib.0.8.0.tar.gz",
    type = "tar.gz",
    sha256 = "2ef429f5d7ce7111263289644d233707dba35e39696377ebab8b0bc701f7818e",
)

# GoogleTest/GoogleMock framework.
# https://github.com/google/googletest/releases/tag/release-1.8.1
http_archive(
    name = "com_google_googletest",
    url = "https://github.com/google/googletest/archive/release-1.8.1.zip",
    strip_prefix = "googletest-release-1.8.1",
    sha256 = "927827c183d01734cc5cfef85e0ff3f5a92ffe6188e0d18e909c5efebf28a0c7",
)

# Abseil
http_archive(
    name = "com_google_absl",
    url = "https://github.com/abseil/abseil-cpp/archive/20190808.zip",
    strip_prefix = "abseil-cpp-20190808",
    sha256 = "0b62fc2d00c2b2bc3761a892a17ac3b8af3578bd28535d90b4c914b0a7460d4e",
)

# Protocol buffers. Used currently only by //rxcppuniq/reactive:poly_value, so if this target
# is not needed, this dependency can be dropped.
# https://github.com/protocolbuffers/protobuf/releases/tag/v3.8.0
http_archive(
    name = "com_google_protobuf",
    urls = ["https://github.com/google/protobuf/archive/v3.8.0.zip"],
    strip_prefix = "protobuf-3.8.0",
    sha256 = "1e622ce4b84b88b6d2cdf1db38d1a634fe2392d74f0b7b74ff98f3a51838ee53",
)

# java_lite_proto_library rules implicitly depend on
# @com_google_protobuf_javalite//:javalite_toolchain, which is the JavaLite proto
# runtime (base classes and common utilities).
http_archive(
    name = "com_google_protobuf_javalite",
    strip_prefix = "protobuf-384989534b2246d413dbcd750744faab2607b516",
    urls = ["https://github.com/google/protobuf/archive/384989534b2246d413dbcd750744faab2607b516.zip"],
    sha256 = "79d102c61e2a479a0b7e5fc167bcfaa4832a0c6aad4a75fa7da0480564931bcc",
)

# This is essentially the same as above ... but named differently. Needed because
# the internals of protobuf rules require us currently so.
http_archive(
    name = "com_google_protobuf_cc",
    urls = ["https://github.com/google/protobuf/archive/v3.8.0.zip"],
    strip_prefix = "protobuf-3.8.0",
    sha256 = "1e622ce4b84b88b6d2cdf1db38d1a634fe2392d74f0b7b74ff98f3a51838ee53",
)

# Used by protobuf.
http_archive(
    name = "zlib",
    urls = ["https://zlib.net/zlib-1.2.11.tar.gz"],
    strip_prefix = "zlib-1.2.11",
    build_file = "@com_google_protobuf//:third_party/zlib.BUILD",
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
)
