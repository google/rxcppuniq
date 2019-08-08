#ifndef RXCPPUNIQ_REACTIVE_REACTIVE_STATUS_H_
#define RXCPPUNIQ_REACTIVE_REACTIVE_STATUS_H_

#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/status.pb.h"

namespace rx {

// Status Code Helpers
// ===================

/**
 * Denotes commonly used status values.
 */
inline Status Done() {
  Status status;
  status.set_code(OUT_OF_RANGE);
  return status;
}
inline Status Ok() {
  Status status;
  status.set_code(OK);
  return status;
}

/**
 * Returns true if the status is either Done or an error.
 */
inline bool IsEnd(Status const& status) { return status.code() != OK; }
template <typename T>
inline bool IsEnd(StatusOr<T> const& status_or) {
  return IsEnd(status_or.status());
}

/**
 * Returns true if the status is done.
 */
inline bool IsDone(Status const& status) {
  return status.code() == OUT_OF_RANGE;
}
template <typename T>
inline bool IsDone(StatusOr<T> const& status_or) {
  return IsDone(status_or.status());
}

/**
 * Returns true if the status is an error, excluding the special code for Done.
 */
inline bool IsError(Status const& status) {
  return status.code() != OK && !IsDone(status);
}
template <typename T>
inline bool IsError(StatusOr<T> const& status_or) {
  return IsError(status_or.status());
}

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_REACTIVE_STATUS_H_
