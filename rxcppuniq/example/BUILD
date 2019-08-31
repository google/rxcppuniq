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

load("//rxcppuniq:config.bzl", "RX_COPTS")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

cc_test(
    name = "examples",
    srcs = [
        "examples.cc",
    ],
    copts = RX_COPTS,
    deps = [
        "//rxcppuniq/base",
        "//rxcppuniq/base:scheduler",
        "//rxcppuniq/reactive",
        "//rxcppuniq/reactive:reactive_testing",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:variant",
    ],
)
