load("//fcp:config.bzl", "FCP_COPTS")


package(
    default_visibility = ["//rxcppuniq:internal"],
    licenses = ["notice"],  # Apache 2.0
)

cc_library(
    name = "reactive",
    srcs = [
        "poly_value.cc",
        "reactive.cc",
        "reactive_impl.inc",
    ],
    hdrs = [
        "object.h",
        "poly_value.h",
        "promise.h",
        "protobuf.h",
        "reactive.h",
        "reactive_status.h",
    ],
    copts = FCP_COPTS,
    deps = [
        "//rxcppuniq/base",
        "//rxcppuniq/base:scheduler",
        "//rxcppuniq/base:status_cc_proto",
        "@com_google_absl//absl/container:fixed_array",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/meta:type_traits",
        "@com_google_absl//absl/types:any",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:variant",
        "@com_google_absl//absl/utility",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "reactive_testing",
    testonly = 1,
    hdrs = [
        "reactive_testing.h",
    ],
    copts = FCP_COPTS,
    deps = [
        ":reactive",
        "//rxcppuniq/base",
        "//rxcppuniq/base:testing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "reactive_test",
    size = "small",
    testonly = 1,
    srcs = [
        "reactive_test.cc",
    ],
    deps = [
        ":reactive",
        ":reactive_testing",
        "//rxcppuniq/base",
        "//rxcppuniq/base:scheduler",
        "//rxcppuniq/base:testing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "promise_test",
    size = "small",
    testonly = 1,
    srcs = [
        "promise_test.cc",
    ],
    deps = [
        ":reactive",
        "//rxcppuniq/base",
        "//rxcppuniq/base:testing",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "poly_value_test",
    size = "small",
    srcs = [
        "poly_value_test.cc",
    ],
    copts = FCP_COPTS,
    deps = [
        ":reactive",
        ":test_cc_proto",
        "//rxcppuniq/base",
        "//rxcppuniq/base:testing",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/memory",
    ],
)

proto_library(
    name = "test_proto",
    srcs = ["testdata/test.proto"],
)

cc_proto_library(
    name = "test_cc_proto",
    deps = [":test_proto"],
)
