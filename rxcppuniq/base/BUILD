# Description:
# Base component, containing common functionality used by other FCP components.

load("//fcp:config.bzl", "FCP_COPTS")


package(
    default_visibility = ["//rxcppuniq:internal"],
    licenses = ["notice"],  # Apache 2.0
)

cc_library(
    name = "base",
    srcs = [
        "monitoring.cc",
        "platform.cc",
    ],
    hdrs = [
        "monitoring.h",
        "move_to_lambda.h",
        "platform.h",
    ],
    copts = FCP_COPTS,
    deps = [
        ":status_cc_proto",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:variant",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "testing",
    testonly = 1,
    srcs = [
        "testing.cc",
    ],
    hdrs = [
        "testing.h",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "monitoring_test",
    size = "small",
    srcs = [
        "monitoring_test.cc",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "platform_test",
    size = "small",
    srcs = [
        "platform_test.cc",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        ":testing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "testing_test",
    size = "small",
    srcs = [
        "testing_test.cc",
    ],
    copts = FCP_COPTS,
    data = [
        "testdata/verify_baseline_test.baseline",
    ],
    deps = [
        ":base",
        ":testing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "scheduler",
    srcs = [
        "scheduler.cc",
    ],
    hdrs = [
        "scheduler.h",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "scheduler_test",
    size = "small",
    srcs = [
        "scheduler_test.cc",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        ":scheduler",
        ":testing",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "move_to_lambda_test",
    size = "small",
    srcs = [
        "move_to_lambda_test.cc",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        ":unique_value",
        "@com_google_googletest//:gtest_main",
    ],
)

proto_library(
    name = "status_proto",
    srcs = ["status.proto"],
)

java_proto_library(
    name = "status_java_proto",
    deps = [":status_proto"],
)

cc_proto_library(
    name = "status_cc_proto",
    deps = [":status_proto"],
)

cc_library(
    name = "unique_value",
    srcs = [
    ],
    hdrs = [
        "unique_value.h",
    ],
    copts = FCP_COPTS,
    deps = ["@com_google_absl//absl/types:optional"],
)

cc_test(
    name = "unique_value_test",
    srcs = [
        "unique_value_test.cc",
    ],
    copts = FCP_COPTS,
    deps = [
        ":base",
        ":unique_value",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/strings",
    ],
)
