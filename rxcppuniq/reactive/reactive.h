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

#ifndef RXCPPUNIQ_REACTIVE_REACTIVE_H_
#define RXCPPUNIQ_REACTIVE_REACTIVE_H_

// Overview
// ========

/**
 * This file contains the basic building blocks for *reactive streams*.
 * Reactive streams are an event driven, flow controlled communication
 * mechanism. They have been derived from the work around <a
 * href="http://reactivex.io/">Reactive Extensions</a> (ReactiveX or RX for
 * short), and are implemented in many languages.
 *
 * Reactive streams do not appear explicitly as types (there is no 'stream'
 * type). Rather, the type `Publisher<T>` is used to represent a *stream
 * builder*. A receiver must *subscribe* to a Publisher<T>, at which point of
 * time the actual stream will be created under the hood.
 *
 * Various operators are available for publishers which allow to create
 * processing pipelines. Here is an example:
 *
 *     Publisher<int>({1, 2, 3})
 *        .Map([](int x) { return x*2; })
 *        .Buffer(2)  // batch in sizes of 2
 *        .Subscribe(
 *          // on_subscribe callback: this is called once at the beginning
 *          // of a stream lifetime.
 *          [](Subscription const& subscription) {
 *            // Request the first element.
 *            subscription.Request(1);
 *          },
 *          // on_next callback: this is called each time a new element
 *          // arrives on the stream, or when the end of the stream is
 *          // reached or an error occured, as indicated by the StatusOr.
 *          [](Subscription const& subscription, StatusOr<std::vector<int>> v) {
 *            if (IsEnd(v)) {
 *              std::cout << "end" << std::endl;
 *            } else {
 *              std::cout << v.ValueOrDie() << std::endl;
 *              // Request the next element.
 *              subscription.Request(1);
 *            }
 *          });
 *
 *     Prints:
 *       {2, 4}
 *       {6}
 *       end
 *
 * Publishers are unique (linear) values, which do not have a copy constructor,
 * but can only be moved. Moreover, in a pipeline like above, each method call
 * consumes the previous instance by having the underlying resources of a
 * publisher moved into the newly created publisher. A publisher is finally
 * moved into the subscription/stream created by the Subscribe method. A
 * stream's lifetime is self-determined: it (and the resources associated with
 * it) live as long as the end-of-stream marker has been send, or the subscriber
 * has cancelled its subscription.
 *
 * More specifically, the above is equivalent to:
 *
 *   auto p = Publisher<int>({1, 2, 3});
 *   p = std::move(p).Map(...);
 *   p = std::move(p).Buffer(...);
 *   std::move(p).Subscribe(...);
 *
 * Note that the model of reactive streams here differs slightly from the one
 * found e.g. in Java and other languages with automatic memory management (see
 * http://reactivex.io), specifically:
 *
 * 1.  Each publisher can have at most one subscriber, which we express
 *     via move semantics of a Publisher.
 * 2.  As seen in the above example, we use `StatusOr<T>` as a generalized
 *     event notification instead of separate methods for next value, error,
 *     and termination. One reason for this is to support C++ move semantics
 *     better. We cannot move a unique context object required for processing
 *     into three different lambdas for handling, therefore we use only one.
 */

#include <memory>
#include <vector>

#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/scheduler.h"
#include "rxcppuniq/reactive/object.h"

namespace rx {

// Subscription (Implementation Level)
// ===================================

/**
 * Interface to a subscription implementation. This interface is typically
 * implemented by private classes which realize different stream types.
 *
 * Pointers to this class should never be used directly, but instead
 * only indirectly via the Subscription class.
 */
class SubscriptionImpl {
 public:
  virtual ~SubscriptionImpl() = default;

  /** See documentation at Subscription::Cancel */
  virtual void Cancel() = 0;

  /** See documentation at Subscription::Request */
  virtual void Request(std::size_t tokens) = 0;
};

namespace reactive_internal {

/**
 * An internal helper type for representing a subscription state.
 *
 * A shared_ptr to this exists both from a Subscription and from
 * a stream implementation. The stream calls clear_subscription() on this
 * when it is deleted, and drops the shared pointer. Subscription
 * may continue to point to the now defunct stream subscription.
 */
class SubscriptionState {
 public:
  explicit SubscriptionState(SubscriptionImpl *impl) : impl_(impl) {}

  inline void clear_subscription() { impl_.store(nullptr); }
  SubscriptionImpl *impl() { return impl_.load(); }

 private:
  std::atomic<SubscriptionImpl *> impl_;
};

}  // namespace reactive_internal

// Subscription (API Level)
// ========================

/**
 * A value representing a stream subscription. This manages access
 * to an underlying raw pointer to SubscriptionImpl* in a safe manner.
 *
 * After the underlying stream is deleted (because it was cancelled or
 * reached end), the predicate has_ended() becomes true on this value, and
 * subsequent calls to other methods will fatally fail.
 *
 * This is currently not thread safe, as there is no need because interaction
 * on streams is serial.
 */
class Subscription {
 public:
  Subscription() = default;

  explicit Subscription(
      std::shared_ptr<reactive_internal::SubscriptionState> const &state)
      : state_(state) {}

  /**
   * Returns true if the stream underlying this subscription has ended.
   */
  inline bool has_ended() const { return !state_->impl(); }

  /**
   * Cancels the subscription to the stream. The stream will be deleted,
   * including the subscriber it holds.
   *
   * Fails if has_ended() is true.
   */
  void Cancel() const {
    RX_CHECK(!has_ended()) << "access to subscription after stream ended";
    state_->impl()->Cancel();
  }

  /**
   * Requests the stream to produce the number of specified events. There is no
   * guarantee that the given number is actually produced, but each
   * implementation guarantees that never more than the requested number are
   * produced.
   *
   * Fails if has_ended() is true.
   */
  void Request(std::size_t count) const {
    RX_CHECK(!has_ended()) << "access to subscription after stream ended";
    state_->impl()->Request(count);
  }

 private:
  std::shared_ptr<reactive_internal::SubscriptionState> state_{};
};

// Subscriber
// ==========

/**
 * Interface to a subscriber.
 *
 * A unique pointer to a subscriber is passed into the Publisher::Subscribe
 * call. Often, subscribers are not implemented in user code via this core
 * interface, but by using lambda-style shortcuts on top of this provided
 * by the Make methods and by shortcuts in the Publisher class.
 *
 * Each of the methods in this interface gets passed in a const& to a
 * Subscription which allows to interact with the connected stream. This
 * is a value which stays valid even after the stream has been deleted (see
 * class Subscription), so can be safely memoized.
 */
template <typename T>
class Subscriber {
 public:
  virtual ~Subscriber() = default;

  /**
   * Method which is called exactly once after this subscriber has been
   * subscribed to a stream. This can be used by the subscriber to perform any
   * initial flow control operations.
   */
  virtual void OnSubscribe(Subscription const &subscription) = 0;

  /**
   * Method which is called whenever an event is generated by the stream. The
   * passed StatusOr<T> can be either OK, in which case the next stream event is
   * passed in, or not OK (IsEnd(event)) indicating a termination of the
   * stream. The special status code OUT_OF_RANGE indicates the case of
   * termination where the end of the stream is reached. See reactive_status.h
   * for shortcuts to interpret the status of an event.
   *
   * Note that the use of the OUT_OF_RANGE status code to indicate stream
   * termination disallows applications to use OUT_OF_RANGE as an actual error
   * condition. This could be seen as a flaw of the current design of this API.
   *
   * TODO(team): Status code OUT_OF_RANGE is used to indicate stream
   * termination
   *
   * Stream implementations guarantee that (a) this method is called serially,
   * and never concurrently for a given stream (b) any other status than OK will
   * be only send once to terminate the stream.
   *
   * After a stream has sent the termination event, it will self-destroy.
   * In course of this also this subscriber will be destroyed. This method
   * is guaranteed to be called before the self-destruction of the stream and
   * the subscriber.
   */
  virtual void OnNext(Subscription const &subscription, StatusOr<T> event) = 0;

  /**
   * Makes a subscriber from functions.
   */
  static std::unique_ptr<Subscriber> Make(
      std::function<void(Subscription const &)> on_subscribe,
      std::function<void(Subscription const &, StatusOr<T>)> on_next);

  /**
   * Makes a subscriber from a function, with automatic flow control.
   *
   * This will automatically request the first element on subscription,
   * and the next one after each call to on_next, provided the stream is not
   * terminated.
   */
  static std::unique_ptr<Subscriber> MakeWithFlowControl(
      std::function<void(StatusOr<T>)> on_next);

  /**
   * This is an internal marker for certain kind of subscriber implementations.
   * It's here so we avoid RTTI while keeping the public APIs small. Do not use.
   */
  virtual int InternalCategory() { return 0; }
};

/**
 * Wrapper around a shared subscriber.
 *
 * This is used to create a unique_ptr<Subscriber> expected by the Subscribe
 * method from a shared_ptr<Subscriber>. Use as in
 *
 *     publisher.Subscribe(SharedSubscriberWrapper<T>::Create(subscriber))
 */
template <typename T>
class SharedSubscriberWrapper
    : public Subscriber<T>,
      public UniqueObjectImpl<SharedSubscriberWrapper<T>> {
 public:
  SharedSubscriberWrapper(std::shared_ptr<Subscriber<T>> const &subscriber)
      : delegate_(subscriber) {}

  SharedSubscriberWrapper(SharedObject<Subscriber<T>> const &subscriber)
      : delegate_(subscriber.impl()) {}

  void OnSubscribe(Subscription const &subscription) override {
    delegate_->OnSubscribe(subscription);
  }

  void OnNext(Subscription const &subscription, StatusOr<T> event) override {
    delegate_->OnNext(subscription, std::move(event));
  }

 private:
  std::shared_ptr<Subscriber<T>> delegate_;
};

// Publisher (Implementation Level)
// ================================

/**
 * Interface to be implemented by a publisher implementation.
 *
 * The implementation provides the core method to subscribe to this publisher.
 * Clients don't use this, but the methods in Publisher<T>.
 */
template <typename T>
class PublisherImpl {
 public:
  virtual ~PublisherImpl() = default;

  /**
   * A helper to call Subscribe moving this/self into the call. This is
   * used by client code.
   */
  static inline void DoSubscribe(std::unique_ptr<PublisherImpl<T>> self,
                                 std::unique_ptr<Subscriber<T>> subscriber) {
    auto ptr = self.get();
    ptr->Subscribe(std::move(self), std::move(subscriber));
  }

 protected:
  /**
   * Method for subscription to this publisher. This is the single
   * entry point which needs to be implemented by a publisher implementation.
   * (For calling this method, see DoSubscribe().)
   *
   * The method takes ownership of its two parameters. The first one is a unique
   * pointer to this (yes, `this`) object itself. The second parameter is the
   * subscriber.
   *
   * The method moves both this (or its parts) and the supplied subscriber into
   * the stream created for the subscription. This stream has a self-controlled
   * lifetime: it lives as long as either the stream is terminated
   * (IsEnd(event)) or the subscription is cancelled by the subscriber.
   */
  virtual void Subscribe(std::unique_ptr<PublisherImpl<T>> self,
                         std::unique_ptr<Subscriber<T>> subscriber) = 0;

 public:
  /**
   * This is an internal marker for certain kind of publisher implementations.
   * It's here so we avoid RTTI while keeping the public APIs small. Do not use.
   *
   * TODO(team): Clarify or resolve the need for this.
   */
  virtual int InternalCategory() { return 0; }
};

// Publisher (API level)
// =====================

/**
 * A unique value representing a stream while it is not yet activated.
 *
 * A stream is activated when publisher.Subscribe() is called. See overview
 * section of this file for an introduction.
 */
template <typename T>
class Publisher : public UniqueObject<PublisherImpl<T>> {
 public:
  using UniqueObject<PublisherImpl<T>>::UniqueObject;

  /**
   * Creates a publisher which publishes zero or more elements. For
   * non-empty list of values, uses a vector. Used as in Publisher<T>({e1,
   * ...}).
   *
   * Note that a std::initializer_list does not support unique values, so
   * if T is unique, you can't construct the vector inline but must
   * initialize it with emplace_back. For similar reasons, this method
   * does not use std::initializer_list as an argument type.
   */
  explicit Publisher();
  explicit Publisher(std::vector<T> values);
  explicit Publisher(T const &value);
  explicit Publisher(T &&value);

  /**
   * Creates a publisher from a generator function.
   */
  template <typename State>
  static Publisher<T> Generator(State initial_state,
                                std::function<StatusOr<T>(State &)> generator);

  /**
   * Creates a publisher which publishes the single given error status.
   */
  static Publisher<T> Error(Status const &status);

  /**
   * Subscribe to this publisher, using the passed in subscriber.
   *
   * See documentation of underlying PublisherImpl::Subscribe.
   */
  void Subscribe(std::unique_ptr<Subscriber<T>> subscriber) && {
    PublisherImpl<T>::DoSubscribe(this->move_impl(), std::move(subscriber));
  }

  /**
   * Subscribe to this publisher in a functional style.
   */
  void Subscribe(
      std::function<void(Subscription const &)> on_subscribe,
      std::function<void(Subscription const &, StatusOr<T>)> on_next) && {
    PublisherImpl<T>::DoSubscribe(
        this->move_impl(),
        Subscriber<T>::Make(std::move(on_subscribe), std::move(on_next)));
  }

  /**
   * Subscribe to this publisher in a functional style, with automatic
   * flow control.
   *
   * This will automatically request the first element on subscription,
   * and the next one after each call to on_next.
   */
  void Subscribe(std::function<void(StatusOr<T> event)> on_next) && {
    PublisherImpl<T>::DoSubscribe(
        this->move_impl(),
        Subscriber<T>::MakeWithFlowControl(std::move(on_next)));
  }

  /**
   * Creates a new publisher which maps values of the underlying publisher
   * according to a function.
   */
  template <typename R>
  Publisher<R> Map(std::function<StatusOr<R>(T)> func) &&;

  /**
   * Casts the publisher to the type R. T x must static_cast<R>(x). Note this
   * does not perform any dynamic type checks.
   */
  template <typename R>
  Publisher<R> StaticCast() &&;

  /**
   * Creates a new publisher which replaces values of the underlying
   * publisher with the stream produced by a function. The elements of that
   * stream are inserted into the output stream, therefore FlatMap.
   */
  template <typename R>
  Publisher<R> FlatMap(std::function<Publisher<R>(T)> func) &&;

  /**
   * Creates a new publisher which first produces the elements of this
   * publisher, and then the elements produced by the passed one. If an error is
   * generated by the 1st publisher, the stream will end there and the 2nd one
   * will never be invoked.
   */
  Publisher<T> Concat(Publisher<T> publisher) &&;

  /**
   * Creates a new publisher which buffers values and publishes them as
   * vectors of a given size. The last returned vector may contain less than
   * size elements.
   */
  Publisher<std::vector<T>> Buffer(std::size_t size) &&;

  /**
   * Creates a publisher which fetches up to count elements and buffers
   * them. The buffer will be refilled not before its fullness drops
   * to the refill_mark (with 0 < refill_mark <= 1). Use a refill_mark
   * of 1 to instruct the Fetch operator to request the next element immediately
   * after an element has been delivered to downstream.
   */
  Publisher<T> Prefetch(std::size_t buffer_size, double refill_mark = 0.5) &&;

  /**
   * Creates a new publisher which merges the results from this publisher
   * and the given publishers. A variant type is used to distinguish values of
   * each publisher. If one of the merged publishers is done, values will
   * be produced from the remaining ones, until all are done. If one of the
   * publishers produces an error, the error will be delivered and the other
   * publishers cancelled.
   *
   * Note that the Merge (and MergeUniform operator below) are not by default
   * thread-safe. In order to use this operator in an asynchronous context,
   * you must ensure that all upstream inputs deliver their result to the same
   * worker, for example:
   *
   *      p1.Async(someWorker1, mergeWorker)
   *        .Merge(p2.Async(someWorker2, mergeWorker)
   *
   * See documentation of Async() for more background.
   */
  template <typename... TS>
  Publisher<absl::variant<T, TS...>> Merge(Publisher<TS>... publisher) &&;

  /**
   * Creates a new publisher which merges the results from this publisher
   * and the given publishers of the same uniform type. This is similar
   * as the Merge operator, but doesn't uses a variant for the output
   * as the elements are of the same type. Consequently, a dynamic
   * number of upstreams can be merged this way.
   *
   * This method comes with some overloads as initializers for vectors
   * cannot deal with move semantics.
   */
  Publisher<T> MergeUniform(Publisher<T> publisher) &&;
  Publisher<T> MergeUniform(Publisher<T> publisher1,
                            Publisher<T> publisher2) &&;
  Publisher<T> MergeUniform(std::vector<Publisher<T>> publisher) &&;

  /**
   * Async creates a publisher which produces elements on one Worker, and
   * consumes them on another one. A Worker guarantees sequential, lock-free
   * asynchronous execution based on an underlying Scheduler. Used as in
   * `pipeline.Async(produce_on, consume_on)`.
   *
   * More specifically, control flow operations like Request and Cancel are
   * executed on the Worker produce_on, while delivery operations like
   * OnSubscribe and OnNext are executed on the Worker consume_on.
   *
   *        produce_on                         consume_on
   *        ==========                         ==========
   *                                               |
   *            <----------- Request ---------------
   *        <produce>
   *           ...
   *            ------------ OnNext --------------->
   *                                            <consume>
   *                                              ...
   *
   * Notice that the position of this operator in a publishing pipeline makes
   * a difference: in A.B.Async(...).C.D, only A and B are effected by the
   * Async operator, whereas C and D are executed solely on the currently
   * active worker (consume_on in this case). In order to ensure that a
   * pipeline is fully produced on a dedicated worker, it is best practice to
   * place this operator at the end of a pipeline.
   *
   * When using this method, you usually should have access to the concept
   * of a "current worker" (for actor implementations, this is the worker
   * associated with the executing actor):
   *
   * 1. Scenario #1: you create a pipeline which you like to be produced
   *    somewhere else but consumed on your current worker:
   *
   *        pipeline.Async(other_worker, current_worker)
   *
   * 2. Scenario #2: you create a pipeline which you produce on the current
   *    worker but which is to be consumed elsewhere:
   *
   *        pipeline.Async(current_worker, other_worker)
   *
   * Notice that in other Rx implementations, one finds this method usually
   * split into separate methods SubscribeOn (for producer) and ObserveOn (for
   * consumer). This API is on a lower abstraction level, making both producer
   * and consumer worker explicit. Abstractions on top of this which track a
   * 'current worker' (of type Worker), e.g. in a thread_local, can re-introduce
   * SubscribeOn/ObserveOn.
   */
  Publisher<T> Async(std::shared_ptr<Worker> const &produce_on,
                     std::shared_ptr<Worker> const &consume_on) &&;
};

/**
 * A type alias for a stream processor -- a function which takes an input stream
 * and produces an output stream.
 */
template <typename I, typename O>
using Processor = std::function<Publisher<O>(Publisher<I>)>;

// Helper Types
// ============

/**
 * A type for representing empty events. Use as in Publisher<Trigger>.
 */
struct Trigger {};

/**
 * A helper for representing a flow control request balance. When a request
 * balance reaches kMax, adding or removing from it will have no effect --
 * it means 'infinite'.
 *
 * This helper is used to implement publishers.
 */
class RequestBalance {
 public:
  static size_t kMax;

  bool HasRequests() const { return balance_ > 0; }
  size_t balance() const { return balance_; }
  void Add(size_t count);
  void Remove(size_t count);
  void reset() { balance_ = 0; }

 private:
  std::size_t balance_{};
};

}  // namespace rx

// Include implementation of open declarations
#include "rxcppuniq/reactive/reactive_impl.inc"

#endif  // RXCPPUNIQ_REACTIVE_REACTIVE_H_
