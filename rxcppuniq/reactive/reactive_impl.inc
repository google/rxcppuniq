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
#include <queue>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
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
    RX_CHECK(self.get() == this);
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
  result.emplace_back(std::forward<T>(value));
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
  RX_CHECK(IsError(status)) << "status must indicate error";
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
    RX_CHECK(base_subscription_) << "OnSubscribe has never been called";
    base_subscription_.value().Request(request_mapping_(tokens));
  }

  void Cancel() override {
    RX_CHECK(base_subscription_) << "OnSubscribe has never been called";
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
    RX_CHECK(self.get() == this);
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
Publisher<std::vector<T>> Publisher<T>::Buffer(std::size_t size) && {
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

// Prefetching Streams
// ===================

namespace reactive_internal {

// Implementation note: complexity in the implementation arises from that the
// upstream of a PrefetchingStream maybe destroyed before all elements in the
// buffer have been delivered. Usually, the lifetime of the downstream is
// coupled to upstream (as we bind the downstream via a Subscriber to the
// upstream), but this does not work for the Prefetch operator. Instead,  we
// need to decouple lifetime of downstream from upstream.
//
// We achieve this by making PrefetchingStream a SharedObject (shared_ptr).
// To the upstream, we pass in a SharedSubscriberWrapper which holds one
// reference to the PrefetchingStream. The other reference is hold by a self
// link in PrefetchingStream. If upstream is destroyed, refcount is decremented
// by one, but downstream stays alive until it clears the reference to self
// after the last element from the buffer has been delivered.

template <typename T>
class PrefetchingStream : public Subscriber<T>,
                          public SubscriptionImpl,
                          public SharedObjectImpl<PrefetchingStream<T>> {
 public:
  // We pass in buffer size and a so-called refill mark which indicates the
  // minimal volume (in percent) the buffer should have before we request
  // refill.
  PrefetchingStream(size_t count, double refill_mark,
                    unique_ptr<Subscriber<T>> subscriber)
      : buffer_size_(count),
        refill_mark_(refill_mark),
        subscription_state_(make_shared<SubscriptionState>(this)),
        subscriber_(std::move(subscriber)) {
    RX_CHECK(count > 0) << "count must be greater 0";
  }

  ~PrefetchingStream() override { subscription_state_->clear_subscription(); }

  // Subscriber listening to the upstream and filling the buffer.

  void OnSubscribe(Subscription const& subscription) override {
    RX_CHECK(!upstream_subscription_) << "OnSubscribe already called";
    upstream_subscription_ = subscription;
    subscriber_->OnSubscribe(Subscription(subscription_state_));
    MaybeFill();
  }

  void OnNext(Subscription const& subscription, StatusOr<T> event) override {
    if (buffer_.size() >= buffer_size_) {
      // This should not happen with strict flow control. We mark the last
      // element of the buffer as an error.
      buffer_.back() =
          RX_STATUS(StatusCode::RESOURCE_EXHAUSTED)
          << "fetch buffer overflow (upstream flow control violation)";
    } else {
      upstream_request_balance_.Remove(1);
      buffer_.emplace(std::move(event));
    }
    // See whether this new data triggers delivery to downstream.
    MaybeDeliver();
  }

  // Subscription being called by the downstream

  void Cancel() override {
    RX_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    upstream_subscription_->Cancel();
  }

  void Request(size_t count) override {
    request_balance_.Add(count);
    // See whether the new request triggers delivery to downstream.
    MaybeDeliver();
  }

  // Locking self pointer. This must be called after construction.
  // This creates an (intentional) cycle which we are clearing explicitly in
  // MaybeDeliver() after we have delivered the last element.
  // TODO(wrwg): document this design pattern in implementors guide
  void LockSelf() { self_lock_ = this->self(); }

 private:
  void MaybeDeliver() {
    if (delivering) {
      // Already in the below loop recursively called via OnNext; prevent
      // entering again.
      return;
    }
    delivering = true;
    while (request_balance_.HasRequests() && buffer_.size() > 0) {
      auto event = std::move(buffer_.front());
      bool is_end = IsEnd(event);
      request_balance_.Remove(1);
      buffer_.pop();
      subscriber_->OnNext(Subscription(subscription_state_), std::move(event));
      if (is_end) {
        // Last element has been delivered: self destruct
        delivering = false;
        self_lock_.reset();
        return;
      }
      MaybeFill();
      if (buffer_.empty()) {
        break;
      }
    }
    delivering = false;
  }

  void MaybeFill() {
    RX_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    if (upstream_subscription_->has_ended()) {
      // Nothing to fill any more
      return;
    }
    if (buffer_.size() >
        static_cast<size_t>(static_cast<double>(buffer_size_) * refill_mark_)) {
      // Buffer considered sufficiently full.
      return;
    }
    auto to_fill = buffer_size_ - buffer_.size();
    if (upstream_request_balance_.balance() >= to_fill) {
      // Already requested elements to fill buffer, but they have not yet
      // arrived.
      return;
    }
    auto request = to_fill - upstream_request_balance_.balance();
    upstream_request_balance_.Add(request);
    upstream_subscription_->Request(request);
  }

  size_t const buffer_size_;
  double const refill_mark_;
  shared_ptr<SubscriptionState> const subscription_state_;
  shared_ptr<PrefetchingStream<T>> self_lock_;
  unique_ptr<Subscriber<T>> subscriber_;
  absl::optional<Subscription> upstream_subscription_;
  std::queue<StatusOr<T>> buffer_;
  RequestBalance request_balance_;
  RequestBalance upstream_request_balance_;
  bool delivering{};
};

template <typename T>
class PrefetchingPublisher : public PublisherImpl<T> {
 public:
  PrefetchingPublisher(unique_ptr<PublisherImpl<T>> upstream,
                       std::size_t buffer_size, double refill_mark)
      : buffer_size_(buffer_size),
        refill_mark_(refill_mark),
        upstream_(std::move(upstream)) {}

 protected:
  void Subscribe(unique_ptr<PublisherImpl<T>> self,
                 unique_ptr<Subscriber<T>> subscriber) override {
    RX_CHECK(self.get() == this);
    auto stream = PrefetchingStream<T>::Create(buffer_size_, refill_mark_,
                                               std::move(subscriber));
    // Lock self pointer in stream, and subscribe the stream using a
    // SharedSubscriberWrapper, which makes a unique_ptr from a shared_ptr.
    // See discussion at PrefetchingStream class.
    stream->LockSelf();
    PublisherImpl<T>::DoSubscribe(std::move(upstream_),
                                  SharedSubscriberWrapper<T>::Create(stream));
  }

 private:
  std::size_t const buffer_size_;
  double const refill_mark_;
  unique_ptr<PublisherImpl<T>> upstream_;
};

}  // namespace reactive_internal

// Implementation of Publisher operators based on PrefetchingPublisher.

template <typename T>
Publisher<T> Publisher<T>::Prefetch(std::size_t buffer_size,
                                    double refill_mark) && {
  return Publisher<T>(
      std::make_unique<reactive_internal::PrefetchingPublisher<T>>(
          this->move_impl(), buffer_size, refill_mark));
}

// Merging Streams
// ===============

namespace reactive_internal {

// For implementing the Merge operator, we need to deal with the problem
// that we cannot directly transpose flow control from a subscriber
// to the upstream sources, because we do not know their pace of production.
// For example, one upstream source may produce many elements at once,
// while another only after some delay.
//
// The implementation therefore subscribes an individual MergeUpstreamSubscriber
// to each upstream which always requests the next element and buffers it once
// it arrives. The merging stream logic than  uses a RequestBalance to manage
// flow control explicitly, polling the upstream buffers in a round-robin way to
// produce the next available element downstream.
//
// Similar as with the PrefetchingStream, this also means that lifetime of
// upstreams and the merging stream are decoupled. We use a SharedObjectImpl
// as base clase and maintain a self() shared_ptr to control lifetime.

// Homogenous base class of upstream subscribers. This allows us to control
// upstream subscribers without knowing there type.
template <typename... TA>
class MergeUpstreamSubscriberBase {
 public:
  virtual ~MergeUpstreamSubscriberBase() {}
  virtual bool HasNext() = 0;
  virtual StatusOr<absl::variant<TA...>> ConsumeNext() = 0;
  virtual void Cancel() = 0;
};

// A helper to get the i-th type of a type parameter pack.
template <size_t I, typename... TA>
using TypeAt = typename std::tuple_element<I, std::tuple<TA...>>::type;

// Heterogenous upstream subscriber. Maintains a lookahead for the next
// element provided from upstream.
template <size_t I, typename... TA>
class MergeUpstreamSubscriber
    : public Subscriber<TypeAt<I, TA...>>,
      public MergeUpstreamSubscriberBase<TA...>,
      public SharedObjectImpl<MergeUpstreamSubscriber<I, TA...>> {
 public:
  using ElemType = TypeAt<I, TA...>;

  explicit MergeUpstreamSubscriber(function<void(int)> state_changed)
      : state_changed_(std::move(state_changed)) {}

  shared_ptr<MergeUpstreamSubscriberBase<TA...>> DoSubscribe(
      unique_ptr<PublisherImpl<ElemType>> publisher) {
    PublisherImpl<ElemType>::DoSubscribe(
        std::move(publisher),
        SharedSubscriberWrapper<ElemType>::Create(this->self()));
    return this->self();
  }

  // Subscriber

  void OnSubscribe(Subscription const& subscription) override {
    RX_CHECK(!upstream_subscription_) << "already subscribed";
    upstream_subscription_ = subscription;
    upstream_subscription_->Request(1);
  }

  void OnNext(Subscription const& subscription,
              StatusOr<ElemType> event) override {
    RX_CHECK(!next_) << "next element not consumed";
    if (IsEnd(event)) {
      next_ = std::move(event).status();
    } else {
      next_ = absl::variant<TA...>(absl::in_place_index_t<I>(),
                                   std::move(event).ValueOrDie());
    }
    // Notify the merge stream that a new element is available.
    state_changed_(1);
  }

  // MergeUpstreamSubscriberBase

  bool HasNext() override { return next_.has_value(); }

  StatusOr<absl::variant<TA...>> ConsumeNext() override {
    RX_CHECK(next_) << "no next element to consume";
    auto result = std::move(next_.value());
    next_.reset();
    // Notify the merge stream that a an element has been consumed.
    state_changed_(-1);
    if (!IsEnd(result)) {
      // Always request the next element after this one has been consumed.
      upstream_subscription_->Request(1);
    }
    return result;
  }

  void Cancel() override {
    RX_CHECK(upstream_subscription_) << "OnSubscribe never called";
    upstream_subscription_->Cancel();
  }

 private:
  function<void(int)> const state_changed_;
  absl::optional<StatusOr<absl::variant<TA...>>> next_;
  absl::optional<Subscription> upstream_subscription_;
};

// MergeStream implementation. Maintains an upstream subscriber
// for each upstream source, and delivers elements to downstream.
template <typename... TA>
class MergeStream : public SubscriptionImpl,
                    public SharedObjectImpl<MergeStream<TA...>> {
 public:
  MergeStream(unique_ptr<Subscriber<absl::variant<TA...>>> subscriber)
      : subscriber_(std::move(subscriber)),
        subscription_state_(make_shared<SubscriptionState>(this)) {}

  template <size_t... IS>
  std::vector<shared_ptr<MergeUpstreamSubscriberBase<TA...>>>
  CreateUpstreamSubscribers(
      absl::index_sequence<IS...>,
      std::tuple<unique_ptr<PublisherImpl<TA>>...> upstream) {
    return {CreateUpstreamSubscriber<IS>(std::move(std::get<IS>(upstream)))...};
  }

  template <size_t I>
  shared_ptr<MergeUpstreamSubscriberBase<TA...>> CreateUpstreamSubscriber(
      unique_ptr<PublisherImpl<TypeAt<I, TA...>>> upstream) {
    auto self = this->self();
    return MergeUpstreamSubscriber<I, TA...>::Create(
               [self](int diff) { self->UpstreamSubscriberStateChanged(diff); })
        ->DoSubscribe(std::move(upstream));
  }

  // Some initialization need to be performed after the instance has been
  // constructed and the this->self() pointer is correctly setup.
  void FinalizeConstruction(
      std::tuple<unique_ptr<PublisherImpl<TA>>...> upstream) {
    // Locking self pointer. This creates an (intentional) cycle which we are
    // clearing explicitly in MaybeDeliver() after we have delivered the last
    // element.
    // TODO(wrwg): document this design pattern in implementors guide
    self_lock_ = this->self();

    // Initialize upstream subscribers. They can deliver elements during
    // construction, so we can't do that as part of the standard ctor.
    upstream_subscribers_ = CreateUpstreamSubscribers(
        absl::index_sequence_for<TA...>{}, std::move(upstream));

    // Confirm subscription.
    subscriber_->OnSubscribe(Subscription(subscription_state_));
  }

  // Initializes this class for the case of a uniform merge operator
  // This expects that MergeStream<T> is used with a single T (the 0-th
  // position in the parameter pack). This allows us to reuse the
  // same logic for Merge() and MergeUniform() operators.
  void FinalizeConstructionUniform(
      std::vector<unique_ptr<PublisherImpl<TypeAt<0, TA...>>>> upstream) {
    self_lock_ = this->self();

    // Initialize upstream subscribers for TypeAt<0, TA>.
    for (int i = 0; i < upstream.size(); ++i) {
      upstream_subscribers_.emplace_back(
          CreateUpstreamSubscriber<0>(std::move(upstream[i])));
    }

    // Confirm subscription.
    subscriber_->OnSubscribe(Subscription(subscription_state_));
  }

  ~MergeStream() override { subscription_state_->clear_subscription(); }

  void Cancel() override {
    // Cancel all upstream subscribers.
    for (auto& subscriber : upstream_subscribers_) {
      if (subscriber) {
        subscriber->Cancel();
        subscriber.reset();
      }
    }
    // Self-destruct
    self_lock_.reset();
  }

  void Request(std::size_t tokens) override {
    request_balance_.Add(tokens);
    MaybeDeliver();
  }

 private:
  void UpstreamSubscriberStateChanged(int diff) {
    available_balance_.Add(diff);
    if (diff > 0) {
      // If upstream subscriber added event, trigger delivery.
      MaybeDeliver();
    }
  }

  void MaybeDeliver() {
    if (delivering_) {
      // Already in below loop, got called back recursively.
      return;
    }
    delivering_ = true;
    // As we are counting the elements actually available via upstream in
    // available_balance, we can safely base the loop below on round robing
    // current_index_. One of the upstream subscribers will still have a value
    // if the loop condition is true.
    while (request_balance_.balance() > 0 && available_balance_.balance() > 0) {
      auto& upstream_subscriber = upstream_subscribers_[current_index_];
      current_index_ =
          (current_index_ + 1) % upstream_subscribers_.size();  // round robin
      if (!upstream_subscriber || !upstream_subscriber->HasNext()) {
        // This upstream either ended or has no new value.
        continue;
      }
      auto event = upstream_subscriber->ConsumeNext();
      if (!IsEnd(event)) {
        // Deliver the event.
        request_balance_.Remove(1);
        subscriber_->OnNext(Subscription(subscription_state_),
                            std::move(event));
        continue;
      }
      // This stream ended, clear its subscriber.
      upstream_subscriber.reset();
      if (IsError(event)) {
        // Propagate the error
        subscriber_->OnNext(Subscription(subscription_state_),
                            std::move(event));
        // Cancel all outstanding upstream subscriptions.
        Cancel();
        return;
      }
      // If all upstream sources are done, send done marker and self-destruct.
      bool done = true;
      for (auto& subscriber : upstream_subscribers_) {
        if (subscriber) {
          done = false;
          break;
        }
      }
      if (done) {
        subscriber_->OnNext(Subscription(subscription_state_), Done());
        self_lock_.reset();
        return;
      }
    }
    delivering_ = false;
  }

  unique_ptr<Subscriber<absl::variant<TA...>>> const subscriber_;
  shared_ptr<SubscriptionState> const subscription_state_;
  std::vector<shared_ptr<MergeUpstreamSubscriberBase<TA...>>>
      upstream_subscribers_;
  shared_ptr<MergeStream<TA...>> self_lock_;
  int current_index_{};
  RequestBalance request_balance_;
  RequestBalance available_balance_;
  bool delivering_{};
};

template <typename... TA>
class MergePublisher : public PublisherImpl<absl::variant<TA...>> {
 public:
  explicit MergePublisher(unique_ptr<PublisherImpl<TA>>... upstream)
      : upstream_(std::move(upstream)...) {}

 protected:
  void Subscribe(
      unique_ptr<PublisherImpl<absl::variant<TA...>>> self,
      unique_ptr<Subscriber<absl::variant<TA...>>> subscriber) override {
    RX_CHECK(self.get() == this);
    auto stream = MergeStream<TA...>::Create(std::move(subscriber));
    stream->FinalizeConstruction(std::move(upstream_));
  }

 private:
  std::tuple<unique_ptr<PublisherImpl<TA>>...> upstream_;
};

template <typename T>
class MergeUniformPublisher : public PublisherImpl<T> {
 public:
  explicit MergeUniformPublisher(
      std::vector<unique_ptr<PublisherImpl<T>>> upstream)
      : upstream_(std::move(upstream)) {}

 protected:
  void Subscribe(unique_ptr<PublisherImpl<T>> self,
                 unique_ptr<Subscriber<T>> subscriber) override {
    RX_CHECK(self.get() == this);
    auto stream = MergeStream<T>::Create(
        absl::make_unique<VariantSubscriberWrapper>(std::move(subscriber)));
    stream->FinalizeConstructionUniform(std::move(upstream_));
  }

 private:
  class VariantSubscriberWrapper : public Subscriber<absl::variant<T>> {
   public:
    VariantSubscriberWrapper(unique_ptr<Subscriber<T>> subscriber)
        : subscriber_(std::move(subscriber)) {}

    void OnSubscribe(Subscription const& subscription) override {
      subscriber_->OnSubscribe(subscription);
    }
    void OnNext(Subscription const& subscription,
                StatusOr<absl::variant<T>> event) override {
      if (IsEnd(event)) {
        subscriber_->OnNext(subscription, std::move(event).status());
      } else {
        subscriber_->OnNext(subscription,
                            absl::get<0>(std::move(event.ValueOrDie())));
      }
    }

   private:
    unique_ptr<Subscriber<T>> subscriber_;
  };

  std::vector<unique_ptr<PublisherImpl<T>>> upstream_;
};

}  // namespace reactive_internal

// Publisher operators based on MergeStream

template <typename T>
template <typename... TS>
Publisher<absl::variant<T, TS...>> Publisher<T>::Merge(
    Publisher<TS>... publisher) && {
  return Publisher<absl::variant<T, TS...>>(
      absl::make_unique<reactive_internal::MergePublisher<T, TS...>>(
          this->move_impl(), publisher.move_impl()...));
}

template <typename T>
Publisher<T> Publisher<T>::MergeUniform(Publisher<T> publisher) && {
  std::vector<std::unique_ptr<PublisherImpl<T>>> publisher_impls;
  publisher_impls.emplace_back(this->move_impl());
  publisher_impls.emplace_back(publisher.move_impl());
  return Publisher<T>(
      absl::make_unique<reactive_internal::MergeUniformPublisher<T>>(
          std::move(publisher_impls)));
}

template <typename T>
Publisher<T> Publisher<T>::MergeUniform(Publisher<T> publisher1,
                                        Publisher<T> publisher2) && {
  std::vector<std::unique_ptr<PublisherImpl<T>>> publisher_impls;
  publisher_impls.emplace_back(this->move_impl());
  publisher_impls.emplace_back(publisher1.move_impl());
  publisher_impls.emplace_back(publisher2.move_impl());
  return Publisher<T>(
      absl::make_unique<reactive_internal::MergeUniformPublisher<T>>(
          std::move(publisher_impls)));
}

template <typename T>
Publisher<T> Publisher<T>::MergeUniform(
    std::vector<Publisher<T>> publishers) && {
  std::vector<std::unique_ptr<PublisherImpl<T>>> publisher_impls;
  publisher_impls.emplace_back(this->move_impl());
  for (auto& pub : publishers) {
    publisher_impls.emplace_back(pub.move_impl());
  }
  return Publisher<T>(
      absl::make_unique<reactive_internal::MergeUniformPublisher<T>>(
          std::move(publisher_impls)));
}

// Concat Streams
// ==============

namespace reactive_internal {

template <typename T>
class ConcatStream : public SubscriptionImpl,
                     public Subscriber<T>,
                     public SharedObjectImpl<ConcatStream<T>> {
 public:
  explicit ConcatStream(unique_ptr<Subscriber<T>> subscriber,
                        unique_ptr<PublisherImpl<T>> continue_with)
      : subscription_state_(std::make_shared<SubscriptionState>(this)),
        subscriber_(std::move(subscriber)),
        continue_with_(std::move(continue_with)) {}

  // Subscribes this instance to the next upstream source.
  void SubscribeNext(unique_ptr<PublisherImpl<T>> upstream) {
    // Self lock this instance as it now controls its own lifetime.
    self_lock_ = this->self();
    // Subscribe to the upstream using a unique wrapper around self.
    PublisherImpl<T>::DoSubscribe(
        std::move(upstream), SharedSubscriberWrapper<T>::Create(this->self()));
  }

  ~ConcatStream() override { subscription_state_->clear_subscription(); }

  // Subscription

  void Cancel() override {
    RX_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    if (!upstream_subscription_->has_ended()) {
      upstream_subscription_->Cancel();
    }
  }

  void Request(std::size_t count) override {
    RX_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    request_balance_.Add(count);
    if (!upstream_subscription_->has_ended()) {
      upstream_subscription_->Request(count);
    }
  }

  // Subscriber

  void OnSubscribe(Subscription const& subscription) override {
    RX_CHECK(!upstream_subscription_ || !is_running_first())
        << "already subscribed";
    upstream_subscription_ = subscription;
    if (is_running_first()) {
      // The first time this is called, confirm subscription.
      subscriber_->OnSubscribe(Subscription(subscription_state_));
    } else if (request_balance_.balance() > 0) {
      // For the second time, if there are some outstanding requests from the
      // first stream which have never been delivered, roll them over to the new
      // subscription.
      upstream_subscription_->Request(request_balance_.balance());
    }
  }

  void OnNext(Subscription const& subscription, StatusOr<T> event) override {
    if (!IsEnd(event)) {
      request_balance_.Remove(1);  // account for delivery
      subscriber_->OnNext(Subscription(subscription_state_), std::move(event));
      return;
    }
    if (IsError(event) || !is_running_first()) {
      subscriber_->OnNext(Subscription(subscription_state_), std::move(event));
      // self-destruct
      self_lock_.reset();
      return;
    }
    // Done marker from the first stream. Switch to the continuation stream.
    upstream_subscription_.reset();
    SubscribeNext(std::move(continue_with_));
  }

 private:
  inline bool is_running_first() { return continue_with_.get() != nullptr; }

  shared_ptr<SubscriptionState> const subscription_state_;
  unique_ptr<Subscriber<T>> subscriber_;
  unique_ptr<PublisherImpl<T>> continue_with_;
  absl::optional<Subscription> upstream_subscription_;
  RequestBalance request_balance_;
  shared_ptr<ConcatStream<T>> self_lock_;
};

template <typename T>
class ConcatPublisher : public PublisherImpl<T> {
 public:
  ConcatPublisher(unique_ptr<PublisherImpl<T>> first,
                  unique_ptr<PublisherImpl<T>> second)
      : first_(std::move(first)), second_(std::move(second)) {}

 protected:
  void Subscribe(unique_ptr<PublisherImpl<T>> self,
                 unique_ptr<Subscriber<T>> subscriber) override {
    RX_CHECK(self.get() == this);
    ConcatStream<T>::Create(std::move(subscriber), std::move(second_))
        ->SubscribeNext(std::move(first_));
  }

 private:
  unique_ptr<PublisherImpl<T>> first_;
  unique_ptr<PublisherImpl<T>> second_;
};

}  // namespace reactive_internal

// Publisher operators based on ConcatStream

template <typename T>
Publisher<T> Publisher<T>::Concat(Publisher<T> publisher) && {
  return Publisher<T>(absl::make_unique<reactive_internal::ConcatPublisher<T>>(
      this->move_impl(), publisher.move_impl()));
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
      Terminate(RX_STATUS(FAILED_PRECONDITION) << "flow control error");
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
    RX_CHECK(base_subscription_) << "OnSubscribe was never called";
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
    RX_CHECK(self.get() == this);
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
                       shared_ptr<Worker> worker,
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
          RX_CHECK(!subscription.has_ended())
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
  AsyncStream(shared_ptr<Worker> const& produce_on,
              shared_ptr<Worker> const& consume_on,
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

  // Subscription

  void Cancel() override {
    RX_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
    auto subscription = upstream_subscription_.value();
    // Schedule operation on producer worker
    produce_on_->Schedule([subscription] { subscription.Cancel(); });
  }

  void Request(std::size_t tokens) override {
    RX_CHECK(upstream_subscription_) << "OnSubscribe has never been called";
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
    RX_CHECK(!upstream_subscription_) << "OnSubscribe already called";
    upstream_subscription_ = subscription;
    // Execute the OnSubscribe method on the consumer worker.
    auto subscription_state = subscription_state_;
    auto subscriber_ptr = subscriber_.get();
    consume_on_->Schedule([subscription_state, subscriber_ptr] {
      RX_CHECK(subscription_state->impl()) << "stale subscriber";
      subscriber_ptr->OnSubscribe(Subscription(subscription_state));
    });
  }

  void OnNext(Subscription const&, StatusOr<T> event) override {
    AsyncOnNextHelper(std::move(event), Subscription(subscription_state_),
                      consume_on_, &subscriber_);
  }

 private:
  shared_ptr<Worker> const produce_on_;
  shared_ptr<Worker> const consume_on_;
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
  AsyncPublisher(shared_ptr<Worker> const& produce_on,
                 shared_ptr<Worker> const& consume_on, Publisher<T> input)
      : produce_on_(produce_on),
        consume_on_(consume_on),
        input_(std::move(input)) {}

 protected:
  void Subscribe(unique_ptr<PublisherImpl<T>> self,
                 unique_ptr<Subscriber<T>> subscriber) override {
    RX_CHECK(self.get() == this);
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
  shared_ptr<Worker> const produce_on_;
  shared_ptr<Worker> const consume_on_;
  Publisher<T> input_;
};

}  // namespace reactive_internal

// Implementation of Publisher operators based on AsyncStream

template <typename T>
Publisher<T> Publisher<T>::Async(std::shared_ptr<Worker> const& produce_on,
                                 std::shared_ptr<Worker> const& consume_on) && {
  return Publisher<T>(absl::make_unique<reactive_internal::AsyncPublisher<T>>(
      produce_on, consume_on, this->move_impl()));
}

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_REACTIVE_IMPL_INC_
