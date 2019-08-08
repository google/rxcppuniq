/*
 * Copyright 2019 Google LLC
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

#ifndef RXCPPUNIQ_REACTIVE_REACTIVE_IMPL_INC_
#define RXCPPUNIQ_REACTIVE_REACTIVE_IMPL_INC_

// Contains definitions of declarations in reactive.h and related helpers.

#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "absl/utility/utility.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/move_to_lambda.h"
#include "rxcppuniq/base/status.pb.h"
#include "rxcppuniq/reactive/object.h"
#include "rxcppuniq/reactive/reactive_status.h"

namespace rx {
namespace reactive_internal {

using absl::make_unique;
using std::function;
using std::make_shared;
using std::shared_ptr;
using std::size_t;
using std::unique_ptr;

// Add any new definitions for InternalCategory here so they
// are in one place.
enum InternalCategory {
  kManagedDomainRemotePublisher = 1,
  kCompilerHelpersFromAnyStructPublisher = 2,
};

}  // namespace reactive_internal

// Functional Subscribers
// ======================

namespace reactive_internal {

/**
 * A subscriber implementation defined by functions. This simply forwards
 * interface calls to the provided functions.
 */
template <typename T>
class FunctionalSubscriber : public Subscriber<T> {
 public:
  FunctionalSubscriber(function<void(Subscription const&)> on_subscribe,
                       function<void(Subscription const&, StatusOr<T>)> on_next)
      : on_subscribe_(std::move(on_subscribe)), on_next_(std::move(on_next)) {}

  void OnNext(Subscription const& subscription, StatusOr<T> event) override {
    on_next_(subscription, std::move(event));
  }

  void OnSubscribe(Subscription const& subscription) override {
    on_subscribe_(subscription);
  }

 private:
  function<void(Subscription const&)> const on_subscribe_;
  function<void(Subscription const&, StatusOr<T>)> const on_next_;
};

}  // namespace reactive_internal

/**
 * Implementation of static factories of Subscriber2 based on functions.
 */

template <typename T>
std::unique_ptr<Subscriber<T>> Subscriber<T>::Make(
    std::function<void(Subscription const&)> on_subscribe,
    std::function<void(Subscription const&, StatusOr<T>)> on_next) {
  return absl::make_unique<reactive_internal::FunctionalSubscriber<T>>(
      std::move(on_subscribe), std::move(on_next));
}

template <typename T>
std::unique_ptr<Subscriber<T>> Subscriber<T>::MakeWithFlowControl(
    std::function<void(StatusOr<T>)> on_next) {
  auto on_next_wrapped = MoveToLambda(std::move(on_next));
  return Make(
      [](Subscription const& subscription) { subscription.Request(1); },
      [on_next_wrapped](Subscription const& subscription, StatusOr<T> event) {
        bool is_end = IsEnd(event);
        (*on_next_wrapped)(std::move(event));
        if (!is_end) {
          // If this was not the last element, request the next one.
          subscription.Request(1);
        }
      });
}

// Generating Streams
// ==================

namespace reactive_internal {

/**
 * Implementation of a generated stream, based on a generator function and
 * a state.
 *
 * The implementation kicks off generating elements when the Request function
 * is called.
 */
template <typename T, typename State>
class GeneratingStream : public SubscriptionImpl {
 public:
  GeneratingStream(State initial_state, function<StatusOr<T>(State&)> generator,
                   std::unique_ptr<Subscriber<T>> subscriber)
      : generator_(std::move(generator)),
        state_(std::move(initial_state)),
        subscriber_(std::move(subscriber)),
        subscription_state_(make_shared<SubscriptionState>(this)) {
    subscriber_->OnSubscribe(Subscription(subscription_state_));
  }

  ~GeneratingStream() override { subscription_state_->clear_subscription(); }

  void Cancel() override {
    // On cancel, delete this stream, which will also cause deletion of the
    // subscriber.
    delete this;
  }

  void Request(size_t count) override {
    // Add the request number of events to the balance. If generation
    // is not running, kick it off. This is not thread safe, which is OK since
    // we do not expect thread interaction (by uniqueness of streams).
    //
    // Note that this method can be called from within the call to the OnNext
    // method of the subscriber (in Generate below). In this case, generating_
    // will be true, and we do not need to call Generate, because it is already
    // ongoing. This treatment enables generation of the stream elements
    // without deep recursion.
    requested_.Add(count);
    if (!generating_) {
      Generate();
    }
  }

 private:
  /**
   * Produces elements as long as there are more requested.
   */
  void Generate() {
    generating_ = true;
    while (requested_.HasRequests()) {
      requested_.Remove(1);
      auto event = generator_(state_);
      bool is_end = IsEnd(event);
      // Make a copy of the subscription_state, such that we can track whether
      // the OnNext call has cancelled (and therefore deleted) this stream.
      auto subscription_state_copy = subscription_state_;
      subscriber_->OnNext(Subscription(subscription_state_), std::move(event));
      if (!subscription_state_copy->impl()) {
        // Call to OnNext lead to deletion of stream, bail out
        return;
      }
      if (is_end) {
        // After the last element has been send, delete this stream, which
        // will also cause deletion of the subscriber.
        delete this;
        return;
      }
    }
    generating_ = false;
  }

  function<StatusOr<T>(State&)> const generator_;
  State state_;
  unique_ptr<Subscriber<T>> subscriber_;
  RequestBalance requested_{};
  bool generating_{};
  shared_ptr<SubscriptionState> subscription_state_;
};

template <typename T, typename State>
class GeneratingPublisher : public PublisherImpl<T> {
 public:
  GeneratingPublisher(State initial_state,
                      function<StatusOr<T>(State&)> generator)
      : generator_(std::move(generator)),
        initial_state_(std::move(initial_state)) {}

  void Subscribe(unique_ptr<PublisherImpl<T>> self,
                 unique_ptr<Subscriber<T>> subscriber) override {
    FCP_CHECK(self.get() == this);
    new GeneratingStream<T, State>(std::move(initial_state_),
                                   std::move(generator_),
                                   std::move(subscriber));
  }

 private:
  function<StatusOr<T>(State&)> generator_;
  State initial_state_;
};

/**
 * Function to create a publisher from a vector of values.
 */
template <typename T>
unique_ptr<PublisherImpl<T>> CreateValuePublisher(std::vector<T> values) {
  // Move values into vector in memory.
  auto values_wrapper =
      MoveToLambda(make_unique<std::vector<T>>(std::move(values)));
  return make_unique<GeneratingPublisher<T, size_t>>(
      0, [values_wrapper](std::size_t& index) -> StatusOr<T> {
        auto& values_ptr = *values_wrapper;
        if (index >= values_ptr->size()) {
          return Done();
        }
        T value = std::move(values_ptr->at(index++));
        return value;
      });
}

template <typename T>
std::vector<T> CreateUniqueVector(T&& value) {
  std::vector<T> result;
  result.emplace_back(std::move(value));
  return result;
}

}  // namespace reactive_internal

/**
 * Implementation of Publisher constructors based on GeneratingPublisher.
 */

template <typename T>
template <typename State>
Publisher<T> Publisher<T>::Generator(
    State initial_state, std::function<StatusOr<T>(State&)> generator) {
  return Publisher<T>(
      absl::make_unique<reactive_internal::GeneratingPublisher<T, State>>(
          std::move(initial_state), std::move(generator)));
}

template <typename T>
Publisher<T>::Publisher()
    : Publisher<T>(
          absl::make_unique<reactive_internal::GeneratingPublisher<T, Trigger>>(
              Trigger(), [](Trigger&) { return Done(); })) {}

template <typename T>
Publisher<T>::Publisher(std::vector<T> values)
    : Publisher<T>(
          reactive_internal::CreateValuePublisher<T>(std::move(values))) {}

template <typename T>
Publisher<T>::Publisher(T&& value)
    : Publisher<T>(reactive_internal::CreateValuePublisher<T>(
          reactive_internal::CreateUniqueVector(std::move(value)))) {}

template <typename T>
Publisher<T>::Publisher(T const& value)
    : Publisher<T>(
          reactive_internal::CreateValuePublisher<T>(std::vector<T>{value})) {}

template <typename T>
Publisher<T> Publisher<T>::Error(Status const& status) {
  FCP_CHECK(IsError(status)) << "status must indicate error";
  return Publisher<T>(
      absl::make_unique<reactive_internal::GeneratingPublisher<T, Trigger>>(
          Trigger(), [status](Trigger&) { return status; }));
}

// Chaining Streams
// ================

/**
 * Implementation of a chaining stream (e.g. publisher.Map). This is based
 * on a function which processes a value from the underlying stream and passes
 * it on to a subscriber, and a function from # of requested elements on the
 * outer stream to the # in the underlying stream. For publisher.Map, this
 * mapping is 1:1. For publisher.Buffer(N), this mapping is 1:N.
 */
namespace reactive_internal {
template <typename T, typename R>
class ChainingStream : public Subscriber<T>, public SubscriptionImpl {
 public:
  using OnNextFunction =
      function<void(Subscription const&, Subscriber<R>*, StatusOr<T>)>;

  ChainingStream(function<size_t(size_t)> request_mapping, OnNextFunction func,
                 unique_ptr<Subscriber<R>> subscriber)
      : request_mapping_(std::move(request_mapping)),
        func_(std::move(func)),
        subscriber_(std::move(subscriber)),
        subscription_state_(make_shared<SubscriptionState>(this)) {}

  ~ChainingStream() override { subscription_state_->clear_subscription(); }

  // Subscription

  void Request(size_t tokens) override {
    FCP_CHECK(base_subscription_) << "OnSubscribe has never been called";
    base_subscription_.value().Request(request_mapping_(tokens));
  }

  void Cancel() override {
    FCP_CHECK(base_subscription_) << "OnSubscribe has never been called";
    if (!base_subscription_.value().has_ended()) {
      base_subscription_.value().Cancel();
    }
  }

  // Subscriber

  void OnSubscribe(Subscription const& subscription) override {
    base_subscription_ = subscription;
    subscriber_->OnSubscribe(Subscription(subscription_state_));
  }

  void OnNext(Subscription const&, StatusOr<T> event) override {
    func_(Subscription(subscription_state_), subscriber_.get(),
          std::move(event));
  }

 private:
  function<size_t(size_t)> const request_mapping_;
  OnNextFunction const func_;
  unique_ptr<Subscriber<R>> const subscriber_;
  shared_ptr<SubscriptionState> const subscription_state_;
  absl::optional<Subscription> base_subscription_{};
};

template <typename T, typename R>
class ChainingPublisher : public PublisherImpl<R> {
 public:
  using OnNextFunction = typename ChainingStream<T, R>::OnNextFunction;

  ChainingPublisher(unique_ptr<PublisherImpl<T>> base,
                    function<size_t(size_t)> request_mapping,
                    OnNextFunction func)
      : base_(std::move(base)),
        request_mapping_(std::move(request_mapping)),
        func_(std::move(func)) {}

  void Subscribe(unique_ptr<PublisherImpl<R>> self,
                 unique_ptr<Subscriber<R>> subscriber) override {
    FCP_CHECK(self.get() == this);
    auto stream = make_unique<ChainingStream<T, R>>(
        std::move(request_mapping_), std::move(func_), std::move(subscriber));
    PublisherImpl<T>::DoSubscribe(std::move(base_), std::move(stream));
  }

 private:
  unique_ptr<PublisherImpl<T>> base_;
  function<size_t(size_t)> request_mapping_;
  OnNextFunction func_;
};

/**
 * Polymorphic casts which work with template parameter inference.
 */
template <typename O, typename I>
void PolyStaticCast(UniqueObject<I> x, UniqueObject<O>* y) {
  *y = std::move(x).template Cast<O>();
}

template <typename O, typename I>
void PolyStaticCast(std::unique_ptr<I> x, std::unique_ptr<O>* y) {
  *y = std::unique_ptr<O>(x.release());
}

template <typename O, typename I>
void PolyStaticCast(SharedObject<I> x, SharedObject<O>* y) {
  *y = std::move(x).template Cast<O>();
}

template <typename O, typename I>
void PolyStaticCast(std::shared_ptr<I> x, std::shared_ptr<O>* y) {
  *y = std::static_pointer_cast<O>(std::move(x));
}

template <typename I, typename O>
void PolyStaticCast(I x, O* y) {
  *y = static_cast<O>(std::move(x));
}

}  // namespace reactive_internal

/**
 * Implementation of Publisher operators based on ChainingPublisherImpl.
 */

template <typename T>
template <typename R>
Publisher<R> Publisher<T>::Map(std::function<StatusOr<R>(T)> func) && {
  auto func_wrapper = MoveToLambda(std::move(func));
  return Publisher<R>(
      absl::make_unique<reactive_internal::ChainingPublisher<T, R>>(
          this->move_impl(), [](std::size_t tokens) { return tokens; },
          [func_wrapper](Subscription const& subscription,
                         Subscriber<R>* subscriber, StatusOr<T> event) {
            if (!IsEnd(event)) {
              subscriber->OnNext(
                  subscription, (*func_wrapper)(std::move(event.ValueOrDie())));
            } else {
              subscriber->OnNext(subscription, event.status());
            }
          }));
}

template <typename T>
template <typename R>
Publisher<R> Publisher<T>::StaticCast() && {
  // TODO(wrwg): we should be able to do faster than a Map using void*
  //   and static asserts. This is intended to be a static cast which is correct
  //   in all instances.
  return Publisher<R>(
      absl::make_unique<reactive_internal::ChainingPublisher<T, R>>(
          this->move_impl(), [](std::size_t tokens) { return tokens; },
          [](Subscription const& subscription, Subscriber<R>* subscriber,
             StatusOr<T> event) {
            if (!IsEnd(event)) {
              R casted;
              reactive_internal::PolyStaticCast(std::move(event.ValueOrDie()),
                                                &casted);
              subscriber->OnNext(subscription, std::move(casted));
            } else {
              subscriber->OnNext(subscription, event.status());
            }
          }));
}

template <typename T>
Publisher<std::vector<T>> Publisher<T>::Buffer(std::int32_t size) && {
  auto buffer = absl::make_unique<std::vector<T>>();
  auto moved_buffer = MoveToLambda(std::move(buffer));
  return Publisher<std::vector<T>>(
      absl::make_unique<
          reactive_internal::ChainingPublisher<T, std::vector<T>>>(
          this->move_impl(),
          [size](std::size_t tokens) { return size * tokens; },
          [size, moved_buffer](Subscription const& subscription,
                               Subscriber<std::vector<T>>* subscriber,
                               StatusOr<T> event) {
            auto& buffer = *moved_buffer;
            if (!IsEnd(event)) {
              buffer->emplace_back(std::move(event.ValueOrDie()));
              if (buffer->size() == size) {
                subscriber->OnNext(subscription, std::move(*buffer));
                buffer->clear();
              }
            } else {
              if (event.status().code() == OUT_OF_RANGE && buffer->size() > 0) {
                // flush remaining elements
                subscriber->OnNext(subscription, std::move(*buffer));
              }
              subscriber->OnNext(subscription, event.status());
            }
            return Ok();
          }));
}

// FlatMap Streams
// ===============

// Implementation note: the main difficulty in FlatMap is the correct
// implementation of flow control. Each element in the base stream will be
// replaced by an unknown, possibly empty number of elements produced by the
// inserted stream. A request for 3 events by the FlatMap subscriber
// may result in requesting 2 events on the base stream, and the resulting 3
// events are produced by two inserted streams (e.g. the first producing 1
// event, the second 2). We therefore use a custom subscription and subscriber
// to keep track of flow control and count the number of events delivered
// explicitly.

namespace reactive_internal {
template <typename T, typename R>
class FlatMapStream : public Subscriber<T>, public SubscriptionImpl {
 public:
  FlatMapStream(function<Publisher<R>(T)> cont,
                unique_ptr<Subscriber<R>> subscriber)
      : cont_(std::move(cont)),
        subscriber_(std::move(subscriber)),
        subscription_state_(make_shared<SubscriptionState>(this)) {}

  ~FlatMapStream() override {
    if (cont_subscription_ && !cont_subscription_.value().has_ended()) {
      cont_subscription_.value().Cancel();
    }
    subscription_state_->clear_subscription();
  }

  // Subscription

  void Cancel() override { base_subscription().Cancel(); }

  void Request(size_t tokens) override {
    request_balance_.Add(tokens);
    if (cont_subscription_) {
      // Forward delta of balance to active continuation.
      auto delta = request_balance_.balance() - cont_requested_.balance();
      if (delta > 0) {
        cont_requested_.Add(delta);
        cont_subscription_.value().Request(delta);
      }
    } else {
      // Only request one element from base subscription. This will be
      // substituted by the continuation result.
      base_subscription().Request(1);
    }
  }

  // Subscriber<T> (for base stream)

  void OnNext(Subscription const&, StatusOr<T> event) override {
    if (IsEnd(event)) {
      // Signal termination to underlying subscriber.
      Send(event.status());
      return;
    }
    // It is a flow control error (base stream produces more than
    // requested) if the continuation isn't processed by now. We do not
    // Request on the base stream if cont_subscription_ is set.
    if (cont_subscription_) {
      Terminate(FCP_STATUS(FAILED_PRECONDITION) << "flow control error");
      return;
    }
    // Compute continuation and subscribe to it.
    //
    // We can capture 'this' in the below lambdas since whenever it is deleted,
    // we ensure that cont_subscription_.has_ended(). This assumes the publisher
    // follows the contract to call OnSubscribe before any other code.
    // TODO(wrwg): we may want to make this foolproof regards memory safety
    cont_(std::move(event.ValueOrDie()))
        .Subscribe(
            // OnSubscribe
            [this](Subscription const& subscription) {
              this->cont_subscription_ = subscription;
              cont_requested_.reset();
              if (request_balance_.HasRequests()) {
                // Call our own request to align this subscription with the
                // current balance. We don't add any new requests so we pass 0.
                this->Request(0);
              }
            },
            // OnNext
            [this](Subscription const&, StatusOr<R> event) {
              this->OnNextCont(std::move(event));
            });
  }

  void OnSubscribe(Subscription const& subscription) override {
    base_subscription_ = subscription;
    subscriber_->OnSubscribe(Subscription(subscription_state_));
  }

 private:
  // Subscriber<R> helper for continuations, keeping track of request balance.
  void OnNextCont(StatusOr<R> event) {
    if (!IsEnd(event)) {
      // Account for this single received event against the balance.
      request_balance_.Remove(1);
      cont_requested_.Remove(1);
      Send(std::move(event));
    } else {
      if (IsDone(event)) {
        // Reset the continuation subscription, as we are done with it.
        // The stream will continue with the next continuation, if any, so
        // do not emit a Done to the underlying subscriber.
        cont_subscription_.reset();
        // If there are more elements requested, ask for the next one
        // from base subscription. by calling our own Request.
        if (request_balance_.HasRequests()) {
          Request(0);
        }
      } else {
        // Error: stop processing, reporting the error to the underlying
        // subscriber.
        Terminate(event.status());
      }
    }
  }

  void Terminate(Status status) {
    Send(std::move(status));
    base_subscription().Cancel();
  }

  inline void Send(StatusOr<R> event) {
    subscriber_->OnNext(Subscription(subscription_state_), std::move(event));
  }

  inline Subscription const& base_subscription() {
    FCP_CHECK(base_subscription_) << "OnSubscribe was never called";
    return base_subscription_.value();
  }

  function<Publisher<R>(T)> const cont_;

  // The subscriber to this FlatMap
  std::unique_ptr<Subscriber<R>> const subscriber_;

  // A pointer to the subscription state.
  shared_ptr<SubscriptionState> const subscription_state_;

  // Contains the balance of requested but not yet delivered events.
  RequestBalance request_balance_{};

  // The balance of events we have already requested from the continuation
  // subscription.
  RequestBalance cont_requested_{};

  // The subscription to the base stream.
  absl::optional<Subscription> base_subscription_{};

  // The optional current subscription to the stream produced by the
  // continuation. Notice that we can only have one of those at a time
  // by properties of strict flow control.
  absl::optional<Subscription> cont_subscription_{};
};

template <typename T, typename R>
class FlatMapPublisherImpl : public PublisherImpl<R> {
 public:
  FlatMapPublisherImpl(std::unique_ptr<PublisherImpl<T>> base,
                       function<Publisher<R>(T)> cont)
      : base_(std::move(base)), cont_(std::move(cont)) {}

  void Subscribe(unique_ptr<PublisherImpl<R>> self,
                 unique_ptr<Subscriber<R>> subscriber) override {
    FCP_CHECK(self.get() == this);
    auto stream = make_unique<FlatMapStream<T, R>>(std::move(cont_),
                                                   std::move(subscriber));
    PublisherImpl<T>::DoSubscribe(std::move(base_), std::move(stream));
  }

 private:
  std::unique_ptr<PublisherImpl<T>> base_;
  function<Publisher<R>(T)> cont_;
};

}  // namespace reactive_internal

// Implementation of Publisher operators based on FlatMapPublisherImpl

template <typename T>
template <typename R>
Publisher<R> Publisher<T>::FlatMap(std::function<Publisher<R>(T)> func) && {
  return Publisher<R>(
      absl::make_unique<reactive_internal::FlatMapPublisherImpl<T, R>>(
          this->move_impl(), std::move(func)));
}

// Async Streams
// =============

namespace reactive_internal {

/**
 * A helper method for processing OnNext asynchronously.
 *
 * If delivery of an event to a subscriber happens asynchronously, the
 * subscribers lifetime needs to be longer than that of the stream. This
 * is because after the end marker of the stream has been delivered the stream
 * is destroyed, and therefore also the subscriber if it is still bound to it.
 *
 * This method deals with this by moving the subscriber into the task which
 * processes the last event. Not before this task finished, the subscriber
 * will be destroyed.
 */
template <typename T>
void AsyncOnNextHelper(StatusOr<T> event, Subscription const& subscription,
                       SharedObject<Worker> worker,
                       UniqueObject<Subscriber<T>>* subscriber) {
  auto ok = event.ok();
  auto wrapped_event = MoveToLambda(std::move(event));
  if (!ok) {
    // Last element of the stream. We move the subscriber into the lambda.
    auto wrapped_subscriber = MoveToLambda(std::move(*subscriber));
    worker->Schedule(
        // Lambda is stateful (but only called once); it consumes
        // wrapped_event.
        [wrapped_event, subscription, wrapped_subscriber]() mutable {
          (*wrapped_subscriber)
              ->OnNext(subscription, std::move(*wrapped_event));
        });
  } else {
    // The raw subscriber pointer should be valid in the lambda, but we can
    // protect against bugs with an orderly crash by using the
    // subscription, which reflects whether the stream and it's bound
    // subscriber is still alive.
    worker->Schedule(
        // Lambda is stateful (but only called once); it consumes
        // wrapped_event.
        [wrapped_event, subscriber, subscription]() mutable {
          FCP_CHECK(!subscription.has_ended())
              << "unexpected call to stale subscriber";
          (*subscriber)->OnNext(subscription, std::move(*wrapped_event));
        });
  }
}

template <typename T>
class AsyncStream : public SubscriptionImpl,
                    public Subscriber<T>,
                    public UniqueObjectImpl<AsyncStream<T>> {
 public:
  AsyncStream(SharedObject<Worker> const& produce_on,
              SharedObject<Worker> const& consume_on,
              UniqueObject<Subscriber<T>> subscriber)
      : produce_on_(produce_on),
        consume_on_(consume_on),
        subscriber_(std::move(subscriber)),
        subscription_state_(std::make_shared<SubscriptionState>(this)) {}

  // Note on thread safety: all fields of the class or const except
  // upstream_subscription. This one is single-assigned by OnSubscribe
  // and later consumed by Cancel and Request. By definition, Cancel and
  // Request cannot be called before or during OnSubscribe, because
  // the subscriber does not get hand on the subscription before OnSubscribe
  // finished.

  ~AsyncStream() override { subscription_state_->clear_subscription(); }

  void Cancel() override {
    FCP_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    auto subscription = upstream_subscription_.value();
    // Schedule operation on producer worker
    produce_on_->Schedule([subscription] { subscription.Cancel(); });
  }

  void Request(std::size_t tokens) override {
    FCP_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    auto subscription = upstream_subscription_.value();
    // Schedule operation in producers worker
    produce_on_->Schedule([tokens, subscription] {
      if (!subscription.has_ended()) {
        subscription.Request(tokens);
      }
    });
  }

  // Subscriber

  void OnSubscribe(Subscription const& subscription) override {
    FCP_CHECK(!upstream_subscription_) << "OnSubscribe already called";
    upstream_subscription_ = subscription;
    // Execute the OnSubscribe method on the consumer worker.
    auto subscription_state = subscription_state_;
    auto subscriber_ptr = subscriber_.get();
    consume_on_->Schedule([subscription_state, subscriber_ptr] {
      FCP_CHECK(subscription_state->impl()) << "stale subscriber";
      subscriber_ptr->OnSubscribe(Subscription(subscription_state));
    });
  }

  void OnNext(Subscription const&, StatusOr<T> event) override {
    AsyncOnNextHelper(std::move(event), Subscription(subscription_state_),
                      consume_on_, &subscriber_);
  }

 private:
  SharedObject<Worker> const produce_on_;
  SharedObject<Worker> const consume_on_;
  UniqueObject<Subscriber<T>> subscriber_;
  std::shared_ptr<SubscriptionState> const subscription_state_;
  absl::optional<Subscription> upstream_subscription_;
};

/**
 * A Publisher for the AsyncStream.
 */
template <typename T>
class AsyncPublisher : public PublisherImpl<T>,
                       public UniqueObjectImpl<AsyncPublisher<T>> {
 public:
  AsyncPublisher(SharedObject<Worker> const& produce_on,
                 SharedObject<Worker> const& consume_on, Publisher<T> input)
      : produce_on_(produce_on),
        consume_on_(consume_on),
        input_(std::move(input)) {}

 protected:
  void Subscribe(unique_ptr<PublisherImpl<T>> self,
                 unique_ptr<Subscriber<T>> subscriber) override {
    FCP_CHECK(self.get() == this);
    // Wrap the subscription procedure into a task executed on the
    // producer worker.
    auto wrapped_input = MoveToLambda(std::move(input_));
    auto wrapped_subscriber = MoveToLambda(std::move(subscriber));
    auto produce_on = produce_on_;
    auto consume_on = consume_on_;
    produce_on_->Schedule(
        [wrapped_input, wrapped_subscriber, produce_on, consume_on]() mutable {
          auto stream = absl::make_unique<AsyncStream<T>>(
              produce_on, consume_on, std::move(*wrapped_subscriber));
          PublisherImpl<T>::DoSubscribe(wrapped_input->move_impl(),
                                        std::move(stream));
        });
  }

 private:
  SharedObject<Worker> const produce_on_;
  SharedObject<Worker> const consume_on_;
  Publisher<T> input_;
};

}  // namespace reactive_internal

// Implementation of Publisher operators based on AsyncStream

template <typename T>
Publisher<T> Publisher<T>::Async(SharedObject<Worker> const& produce_on,
                                 SharedObject<Worker> const& consume_on) && {
  return Publisher<T>(absl::make_unique<reactive_internal::AsyncPublisher<T>>(
      produce_on, consume_on, this->move_impl()));
}

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_REACTIVE_IMPL_INC_
