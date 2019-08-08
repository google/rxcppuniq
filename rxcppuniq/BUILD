# Description
# ===========
#
# RxCppUniq is an implementation of reactive streams (reactive-streams.org)
# which is tailored for the C++ unique programming model (move semantics).
# It also supports (and only supports) strict flow control.

package(default_visibility = [":internal"])

licenses(["notice"])  # Apache 2.0

exports_files([
    "LICENSE",
])

package_group(
    name = "internal",
    packages = [
        "//third_party/rxcpp_uniq/...",
    ],
)
