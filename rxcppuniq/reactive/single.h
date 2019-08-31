/*
 * Copyright 2018 Google LLC
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

#ifndef RXCPPUNIQ_REACTIVE_SINGLE_H_
#define RXCPPUNIQ_REACTIVE_SINGLE_H_

#include "rxcppuniq/reactive/reactive.h"

namespace rx {

/**
 * A Single is a wrapper around a a stream which delivers a single element.
 * Any stream can be turned into a Single which delivers at least one element
 * or an error.
 *
 * The API for Singles is different than for streams. Though they are
 * conceptually similar, there are some pragmatic differences in usage:
 * flow control is implicit, and an end-of-stream marker is not needed.
 *
 * Singles as defined here are, like Publishers, unique and not active
 * before someone calls Subscribe() on them. This also means that singles can
 * be remoted to build single pipelines, as no execution is happening until
 * requested.
 */
template <typename T>
class Single {
 public:
  Single() = default;

  /**
   * Creates a single which produces the given value.
   */
  explicit Single(T value) : publisher_(Publisher<T>(std::move(value))) {}

  /**
   * Creates a single which produces the given error.
   */
  static Single<T> Error(Status status) {
    return Single<T>(Publisher<T>::Error(status));
  }

  /**
   * Creates a single from any publisher which produces at least one element
   * or an error. After the first element, the subscription will be canceled,
   * if it is still active.
   */
  explicit Single(Publisher<T> publisher) : publisher_(std::move(publisher)) {}

  /**
   * Subscribes to the single and listens to the result. This consumes
   * the single.
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
                  // This violates the contract for a single: there must be at
                  // least one element or an error.
                  called = true;
                  listener(RX_STATUS(FAILED_PRECONDITION)
                           << "unexpected end-of-stream for single");
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
   * Continues with the function if the single returns a result, and
   * propagate the status if it returns an error.
   */
  template <typename R>
  Single<R> ContinueWith(std::function<Single<R>(T)> func) && {
    auto wrapped = MoveToLambda(std::move(func));
    return Single<R>(
        std::move(publisher_).template FlatMap<R>([wrapped](T value) {
          auto const& func = *wrapped;
          return std::move(func(std::move(value)).publisher_);
        }));
  }

  /**
   * Moves the underlying stream out of the single.
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

#endif  // RXCPPUNIQ_REACTIVE_SINGLE_H_
