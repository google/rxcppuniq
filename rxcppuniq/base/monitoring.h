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
#ifndef RXCPPUNIQ_BASE_MONITORING_H_
#define RXCPPUNIQ_BASE_MONITORING_H_

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/types/variant.h"
#include "rxcppuniq/base/status.pb.h"  // IWYU pragma: export

namespace rx {

// General Definitions
// ===================

/**
 * Indicates to run the functionality in this file in debugging mode. This
 * creates more exhaustive diagnostics in some cases, as documented case by
 * case.
 *
 * We may want to make this a flag if this turns out to be often needed.
 */
constexpr bool rx_debug = false;

// Logging and Assertions
// ======================

/**
 * Defines a subset of Google style logging. Use RX_LOG(INFO),
 * RX_LOG(WARNING), RX_LOG(ERROR) or RX_LOG(FATAL) to stream log messages.
 * Example:
 *
 *     RX_LOG(INFO) << "some info log";
 */
// TODO(team): adapt to absl logging once available
#define RX_LOG(severity) _RX_LOG_##severity

#define RX_LOG_IF(severity, condition) \
  !(condition) ? (void)0               \
               : ::rx::internal::LogMessageVoidify() & _RX_LOG_##severity

#define _RX_LOG_INFO \
  ::rx::internal::LogMessage(__FILE__, __LINE__, ::absl::LogSeverity::kInfo)
#define _RX_LOG_WARNING \
  ::rx::internal::LogMessage(__FILE__, __LINE__, ::absl::LogSeverity::kWarning)
#define _RX_LOG_ERROR \
  ::rx::internal::LogMessage(__FILE__, __LINE__, ::absl::LogSeverity::kError)
#define _RX_LOG_FATAL \
  ::rx::internal::LogMessage(__FILE__, __LINE__, ::absl::LogSeverity::kFatal)

/**
 * Check that the condition holds, otherwise die. Any additional messages can
 * be streamed into the invocation. Example:
 *
 *     RX_CHECK(condition) << "stuff went wrong";
 */
#define RX_CHECK(condition) \
  RX_LOG_IF(FATAL, !(condition)) << ("Check failed: " #condition ". ")

/**
 * Check that the expression generating a status code is OK, otherwise die.
 * Any additional messages can be streamed into the invocation.
 */
#define RX_CHECK_STATUS(status)                                            \
  for (auto __check_status = (status); __check_status.code() != ::rx::OK;) \
  RX_LOG_IF(FATAL, __check_status.code() != ::rx::OK)                      \
      << "status not OK: " << __check_status.DebugString()

// Logging Implementation Details
// ==============================

namespace internal {

/**
 * An object which implements a logger sink. The default sink sends log
 * messages to stderr.
 */
class Logger {
 public:
  virtual ~Logger() {}

  /**
   * Basic log function.
   *
   * @param file The name of the associated file.
   * @param line  The line in this file.
   * @param severity  Severity of the log message.
   * @param message  The message to log.
   */
  virtual void Log(const char* file, int line, absl::LogSeverity severity,
                   const char* message);
};

/**
 * Holds the active logger object.
 *
 * This possible needs to be refined to be thread safe for updaters.
 */
extern Logger* logger;

/**
 * Object allowing to construct a log message by streaming into it. This is
 * used by the macro LOG(severity).
 */
class LogMessage {
 public:
  LogMessage(const char* file, int line, absl::LogSeverity severity)
      : file_(file), line_(line), severity_(severity) {}

  template <typename T>
  LogMessage& operator<<(const T& x) {
    message_ << x;
    return *this;
  }

  LogMessage& operator<<(std::ostream& (*pf)(std::ostream&)) {
    message_ << pf;
    return *this;
  }

  ~LogMessage() {
    logger->Log(file_, line_, severity_, message_.str().c_str());
    if (severity_ == absl::LogSeverity::kFatal) {
      abort();
    }
  }

 private:
  const char* file_;
  int line_;
  absl::LogSeverity severity_;
  std::ostringstream message_;
};

/**
 * This class is used to cast a LogMessage instance to void within a ternary
 * expression in the expansion of RX_LOG_IF.  The cast is necessary so
 * that the types of the expressions on either side of the : match, and the &
 * operator is used because its precedence is lower than << but higher than
 * ?:.
 */
class LogMessageVoidify {
 public:
  void operator&(LogMessage&){};
};

}  // namespace internal

// Status
// ======

// Note that the type Status is currently a proto defined in status.proto.
// This proto is re-exported by this header.

/**
 * Constructor for a status. A status message can be streamed into it. This
 * captures the current file and line position and includes it into the status
 * message if the status code is not OK.
 *
 * Use as in:
 *
 *   RX_STATUS(OK);                // signal success
 *   RX_STATUS(code) << message;   // signal failure
 *
 * RX_STATUS can be used in places which either expect a Status or a
 * StatusOr<T>.
 *
 * You can configure the constructed status to also emit a log entry if the
 * status is not OK by using LogInfo, LogWarning, LogError, or LogFatal as
 * below:
 *
 *   RX_STATUS(code).LogInfo() << message;
 *
 * If the constant rx_debug is true, by default, all RX_STATUS invocations
 * will be logged on INFO level.
 */
#define RX_STATUS(code) \
  ::rx::internal::MakeStatusBuilder(code, __FILE__, __LINE__)

/**
 * Function which allows to check an OK status.
 */
inline bool IsOK(Status status) { return status.code() == OK; }

template <typename T>
class StatusOr;
namespace internal {
/** Functions to assist with RX_RETURN_IF_ERROR() */
inline const rx::Status AsStatus(const Status& status) { return status; }
template <typename T>
inline const rx::Status AsStatus(const rx::StatusOr<T>& status_or) {
  return status_or.status();
}
}  // namespace internal

/**
 * Macro which allows to check for a Status (or StatusOr) and return from the
 * current method if not OK. Example:
 *
 *     Status DoSomething() {
 *       RX_RETURN_IF_ERROR(Step1());
 *       RX_RETURN_IF_ERROR(Step2ReturningStatusOr().status());
 *       return RX_STATUS(OK);
 *     }
 */
#define RX_RETURN_IF_ERROR(expr)                            \
  do {                                                      \
    ::rx::Status __status = ::rx::internal::AsStatus(expr); \
    if (__status.code() != ::rx::StatusCode::OK) {          \
      return (__status);                                    \
    }                                                       \
  } while (false)

/**
 * Macro which allows to check for a StatusOr and return it's status if not OK,
 * otherwise assign the value in the StatusOr to variable or declaration. Usage:
 *
 *     StatusOr<bool> DoSomething() {
 *       RX_ASSIGN_OR_RETURN(auto value, TryComputeSomething());
 *       if (!value) {
 *         RX_ASSIGN_OR_RETURN(value, TryComputeSomethingElse());
 *       }
 *       return value;
 *     }
 */
#define RX_ASSIGN_OR_RETURN(lhs, expr) \
  _RX_ASSIGN_OR_RETURN_1(              \
      _RX_ASSIGN_OR_RETURN_CONCAT(statusor_for_aor, __LINE__), lhs, expr)

#define _RX_ASSIGN_OR_RETURN_1(statusor, lhs, expr) \
  auto statusor = (expr);                           \
  if (!statusor.ok()) {                             \
    return statusor.status();                       \
  }                                                 \
  lhs = std::move(statusor).ValueOrDie();

// See https://goo.gl/x3iba2 for the reason of this construction.
#define _RX_ASSIGN_OR_RETURN_CONCAT(x, y) \
  _RX_ASSIGN_OR_RETURN_CONCAT_INNER(x, y)
#define _RX_ASSIGN_OR_RETURN_CONCAT_INNER(x, y) x##y

namespace internal {
/**
 * Delivers pointer to a constant OK status.
 */
Status const* OkStatus();
}  // namespace internal

/**
 * A value which holds a status, plus a result value if and only if the status
 * is OK.
 */
template <typename T>
class ABSL_MUST_USE_RESULT StatusOr {
 public:
  // Copyable
  StatusOr(const StatusOr& other) = default;
  StatusOr(StatusOr&& other) = default;
  StatusOr& operator=(const StatusOr& other) = default;
  StatusOr& operator=(StatusOr&& other) = default;

  /**
   * Construct a StatusOr from a failed status. The passed status must not be
   * OK.
   */
  inline StatusOr(const Status& status)
      : repr_(Repr(absl::in_place_index_t<kStatusIndex>(), status)) {
    RX_CHECK(status.code() != OK);
  }

  /**
   * Construct a StatusOr from a status code.
   */
  explicit inline StatusOr(StatusCode code)
      : repr_(Repr(absl::in_place_index_t<kStatusIndex>(), Status())) {
    RX_CHECK(code != OK);
    std::get<kStatusIndex>(repr_).set_code(code);
  }

  /**
   * Construct a StatusOr from a status code and a message.
   */
  inline StatusOr(StatusCode code, const std::string& message)
      : repr_(Repr(absl::in_place_index_t<kStatusIndex>(), Status())) {
    RX_CHECK(code != OK);
    std::get<kStatusIndex>(repr_).set_code(code);
    std::get<kStatusIndex>(repr_).set_message(message);
  }

  /**
   * Construct a StatusOr from a value.
   */
  inline StatusOr(T&& value)
      : repr_(Repr(absl::in_place_index_t<kValueIndex>(), std::move(value))) {}

  inline StatusOr(const T& value)
      : repr_(Repr(absl::in_place_index_t<kValueIndex>(), value)) {}

  /**
   * Test whether this StatusOr is OK and has a value.
   */
  inline bool ok() const { return repr_.index() == kValueIndex; }

  /**
   * Return the status. It will be OK if the StatusOr has a value.
   */
  inline Status const& status() const {
    return repr_.index() == kValueIndex ? *internal::OkStatus()
                                        : absl::get<kStatusIndex>(repr_);
  }

  /**
   * Return the value if the StatusOr is ok, otherwise die.
   */
  inline const T& ValueOrDie() const& {
    RX_CHECK(ok()) << "StatusOr has no value";
    return absl::get<kValueIndex>(repr_);
  }
  inline T& ValueOrDie() & {
    RX_CHECK(ok()) << "StatusOr has no value";
    return absl::get<kValueIndex>(repr_);
  }
  inline T&& ValueOrDie() && {
    RX_CHECK(ok()) << "StatusOr has no value";
    return absl::get<kValueIndex>(std::move(repr_));
  }

  /**
   * Used to explicitly ignore a StatusOr (avoiding unused-result warnings).
   */
  void Ignore() const {}

 private:
  enum Indices : std::size_t {
    kStatusIndex = 0,
    kValueIndex = 1,
  };
  using Repr = absl::variant<Status, T>;

  Repr repr_{};
};

/**
 *  Function which combines two status values. Currently, the underlying
 *  representation of Status does not really allow joining, so we return
 *  the 2nd status if the first one is ok, otherwise the first.
 *  is OK.
 */
inline Status CombineStatus(Status const& s1, Status const& s2) {
  return s1.code() == OK ? s2 : s1;
}

// Status Implementation Details
// =============================

namespace internal {

/**
 * Helper class which allows to construct a status with message by streaming
 * into it. Implicitly converts to Status and StatusOr so can be used as a drop
 * in replacement when those types are expected.
 */
class ABSL_MUST_USE_RESULT StatusBuilder {
 public:
  /** Construct a StatusBuilder from status code. */
  StatusBuilder(StatusCode code, const char* file, int line);

  /**
   * Copy constructor for status builder. Most of the time not needed because of
   * copy ellision. */
  StatusBuilder(StatusBuilder const& other);

  /** Return true if the constructed status will be OK. */
  inline bool ok() const { return code_ == OK; }

  /** Returns the code of the constructed status. */
  inline StatusCode code() const { return code_; }

  /** Stream into status message of this builder. */
  template <typename T>
  StatusBuilder& operator<<(T x) {
    message_ << x;
    return *this;
  }

  /** Mark this builder to emit a log message when the result is constructed. */
  inline StatusBuilder& LogInfo() {
    log_severity_ = absl::LogSeverity::kInfo;
    return *this;
  }

  /** Mark this builder to emit a log message when the result is constructed. */
  inline StatusBuilder& LogWarning() {
    log_severity_ = absl::LogSeverity::kWarning;
    return *this;
  }

  /** Mark this builder to emit a log message when the result is constructed. */
  inline StatusBuilder& LogError() {
    log_severity_ = absl::LogSeverity::kError;
    return *this;
  }

  /** Mark this builder to emit a log message when the result is constructed. */
  inline StatusBuilder& LogFatal() {
    log_severity_ = absl::LogSeverity::kFatal;
    return *this;
  }

  /** Implicit conversion to Status. */
  operator Status();

  /** Implicit conversion to StatusOr. */
  template <typename T>
  inline operator StatusOr<T>() {
    return static_cast<Status>(*this);
  }

 private:
  static constexpr absl::LogSeverity kNoLog =
      static_cast<absl::LogSeverity>(-1);
  const char* const file_;
  const int line_;
  const StatusCode code_;
  std::ostringstream message_;
  absl::LogSeverity log_severity_ =
      rx_debug ? absl::LogSeverity::kInfo : kNoLog;
};

/** Workaround for ABSL_MUST_USE_RESULT ignoring constructors */
inline StatusBuilder MakeStatusBuilder(StatusCode code, const char* file,
                                       int line) {
  return StatusBuilder(code, file, line);
}

}  // namespace internal
}  // namespace rx

#endif  // RXCPPUNIQ_BASE_MONITORING_H_
