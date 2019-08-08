#ifndef RXCPPUNIQ_REACTIVE_PROMISE_H_
#define RXCPPUNIQ_REACTIVE_PROMISE_H_

#include "rxcppuniq/reactive/reactive.h"

namespace rx {

/**
 * A promise is a wrapper around a a stream which delivers a single element.
 * Any stream can be turned into a promise which delivers at least one element
 * or an error.
 *
 * The API for promises is different than for streams. Though they are
 * conceptually similar, there are some pragmatic differences in usage:
 * flow control is implicit, and an end-of-stream marker is not needed.
 *
 * Promises as defined here are, like Publishers, unique and not active
 * before someone calls Subscribe() on them. This also means that promises can
 * be remoted to build promise pipelines, as no execution is happening until
 * requested.
 */
template <typename T>
class Promise {
 public:
  Promise() = default;

  /**
   * Creates a promise which produces the given value.
   */
  explicit Promise(T value) : publisher_(Publisher<T>(std::move(value))) {}

  /**
   * Creates a promise which produces the given error.
   */
  static Promise<T> Error(Status status) {
    return Promise<T>(Publisher<T>::Error(status));
  }

  /**
   * Creates a promise from any publisher which produces at least one element
   * or an error. After the first element, the subscription will be canceled,
   * if it is still active.
   */
  explicit Promise(Publisher<T> publisher) : publisher_(std::move(publisher)) {}

  /**
   * Subscribes to the promise and listens to the result. This consumes
   * the promise.
   *
   * TODO(team): Note that currently, a promise does not start computing
   * its result until someone subscribes to it. It is a matter of ongoing
   * discussion whether instead we want "hot" promise semantics.
   */
  void Subscribe(std::function<void(StatusOr<T>)> listener) && {
    struct State {
      std::function<void(StatusOr<T>)> listener;
      bool called;
    };
    auto state = State{std::move(listener), false};
    auto wrapped = MoveToLambda(std::move(state));
    std::move(publisher_)
        .Subscribe(
            [](Subscription const& subscription) { subscription.Request(1); },
            // Lambda has mutable state (State::called)
            [wrapped](Subscription const& subscription,
                      StatusOr<T> next) mutable {
              auto const& listener = wrapped->listener;
              auto& called = wrapped->called;
              if (IsDone(next)) {
                if (!called) {
                  // This violates the contract for a promise: there must be at
                  // least one element or an error.
                  called = true;
                  listener(FCP_STATUS(FAILED_PRECONDITION)
                           << "unexpected end-of-stream for promise");
                }
                return;
              }
              called = true;
              auto is_end = IsEnd(next);
              listener(std::move(next));
              // TODO(team): we shouldn't need the is_end check, the
              //   subscription should just return has_ended() == true in this
              //   case. But currently accessing it crashes.
              if (!is_end && !subscription.has_ended()) {
                subscription.Cancel();
              }
            });
  }

  /**
   * Continues with the function if the promise returns a result, and
   * propagate the status if it returns an error.
   */
  template <typename R>
  Promise<R> ContinueWith(std::function<Promise<R>(T)> func) && {
    auto wrapped = MoveToLambda(std::move(func));
    return Promise<R>(
        std::move(publisher_).template FlatMap<R>([wrapped](T value) {
          auto const& func = *wrapped;
          return std::move(func(std::move(value)).publisher_);
        }));
  }

  /**
   * Moves the underlying stream out of the promise.
   *
   * TODO(team): we want to enforce cardinality if we move the publisher
   *   out of here. Linked bug suggests to introduce Take(N) stream operator
   * which can be also used to implement this property.
   */
  Publisher<T> move_publisher() && { return std::move(publisher_); }

 private:
  Publisher<T> publisher_;
};

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_PROMISE_H_
