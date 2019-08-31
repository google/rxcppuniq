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

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "absl/types/variant.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/scheduler.h"
#include "rxcppuniq/reactive/reactive.h"
#include "rxcppuniq/reactive/reactive_testing.h"

namespace rx_example {
namespace {

using testing::ElementsAre;

// ---------------------------------------------------------------------------
// Basics
// ---------------------------------------------------------------------------

// This example illustrates basic usage of reactive streams.

TEST(Examples, Basic) {
  // Creates a publisher which generates numbers.
  auto pub1 = rx::Publisher<int>({1, 2, 3});

  // Add one to each number. Notice that publisher methods require rvalues,
  // so we need to call move (this is not required if we already have an
  // rvalue, e.g. in rx::Publisher({...}).Map<int>(...)).
  auto pub2 = std::move(pub1).Map<int>([](int x) { return x + 1; });

  // Create batches of size 2
  auto pub3 = std::move(pub2).Buffer(2);

  // Subscibe and store the result into a vector. Notice that subscription
  // consumes the publisher.
  std::vector<std::vector<int>> output;
  std::move(pub3).Subscribe(rx::BufferingSubscriber(output));
  EXPECT_THAT(output, ElementsAre(std::vector<int>{2, 3}, std::vector<int>{4}));

  // Below has the same meaning as above but uses fluent notation.
  output.clear();
  rx::Publisher<int>({1, 2, 3})
      .Map<int>([](int x) { return x + 1; })
      .Buffer(2)
      .Subscribe(rx::BufferingSubscriber(output));
  EXPECT_THAT(output, ElementsAre(std::vector<int>{2, 3}, std::vector<int>{4}));
}

// ---------------------------------------------------------------------------
// Sieve of Eratosthenes
// ---------------------------------------------------------------------------

// This example illustrates more usage of RxCpp. We code a version of the
// sieve of Eratosthenes for generating prime numbers
// (https://en.wikipedia.org/wiki/Sieve_of_Eratosthenes). Several helpers
// are introduced (some of those may become part of the core library, though
// they are easy to express by means of the existing core library).
//
// This implementation of the sieve uses a mutable predicate for filtering and
// is modeled after the Java8 solution found here:
// https://stackoverflow.com/questions/43760641/java-8-streams-and-the-sieve-of-eratosthenes.

/**
 * Generates an infinite stream of integers starting from the given number.
 */
rx::Publisher<int> From(int from) {
  return rx::Publisher<int>::Generator<int>(from, [](int& n) { return n++; });
}

/**
 * Filters an integer stream based on a predicate.
 */
rx::Publisher<int> Filter(rx::Publisher<int> ints,
                          std::function<bool(int)> predicate) {
  return std::move(ints).FlatMap<int>([predicate](int x) {
    return predicate(x) ? rx::Publisher<int>(x) : rx::Publisher<int>();
  });
}

/**
 * Peforms an action for each stream element encountered, producing
 * the input as it is passed in.
 */
rx::Publisher<int> Do(rx::Publisher<int> ints,
                      std::function<void(int)> action) {
  return std::move(ints).Map<int>([action](int x) {
    action(x);
    return x;
  });
}

TEST(Examples, SieveOfEratosthenes) {
  // We maintain a mutable evolving_is_prime predicate. This will be stepwise
  // refined as we discover primes.
  std::function<bool(int)> evolving_is_prime = [](int x) { return true; };

  // Generate numbers and filter them. It is important that we pass in the
  // prime predicate by reference, as it will be changing over time.
  auto filtered = Filter(
      From(2), [&evolving_is_prime](int i) { return evolving_is_prime(i); });

  // Refine the evolving_is_prime predicate with each new prime we discover.
  auto primes = Do(std::move(filtered), [&evolving_is_prime](int prime) {
    evolving_is_prime = [evolving_is_prime /*copy of old predicate*/,
                         prime](int v) {
      return evolving_is_prime(v) && v % prime != 0;
    };
  });

  // Subscribe and test some outputs.
  rx::Subscription subscription;
  int last;
  std::move(primes).Subscribe(
      // on_subscribe
      [&subscription](rx::Subscription const& s) { subscription = s; },
      // on_next
      [&last](rx::Subscription const&, rx::StatusOr<int> elem) {
        last = elem.ValueOrDie();
      });
  subscription.Request(1);
  ASSERT_EQ(last, 2);
  subscription.Request(1);
  ASSERT_EQ(last, 3);
  subscription.Request(1);
  ASSERT_EQ(last, 5);
  subscription.Request(1);
  ASSERT_EQ(last, 7);
  subscription.Request(1);
  ASSERT_EQ(last, 11);

  // Since this is an infinite stream, it is important to cancel the
  // subscription (leading to stream destruction), otherwise we create a leak.
  subscription.Cancel();
}

// ---------------------------------------------------------------------------
// State Machines
// ---------------------------------------------------------------------------

// This example illustrates how to build stream processors which work on
// variants of input types. It uses the C++ 17 std::visit facility to dispatch
// over a std::variant type (actually it uses Abseil to be compatible with older
// C++ versions).
//
// We first define a generic base class which handles the input dispatching,
// then construct as an example a simulation of a 'coffee machine', which is a
// popular example for illustrating [Mealy
// machines](https://en.wikipedia.org/wiki/Mealy_machine). Mealy machines are
// state machines where the transitions are labelled with inputs and
// outputs.

/**
 * Base class of state machines. A StateMachine is a CRTP
 * (https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern_class)
 * which processes the variants of an input and updates its internal state
 * accordingly. Subclasses provide operator()(T) implementations for processing
 * different types of a variant input.
 *
 * Use as in:
 *
 *     class MyMachine : public StateMachine<MyMachine, Input, Output> {
 *       rx::Publisher<Output> operator()(I...) { ... }
 *     }
 *
 * ... where Input = variant<I...> and Output arbitrary.
 */
template <typename Impl, typename Input, typename Output>
class StateMachine : public rx::SharedObjectImpl<Impl> {
 public:
  rx::Publisher<Output> Apply(rx::Publisher<Input> input) {
    // SharedObjectImpl::self() provides a shared_ptr to this instance, which we
    // can safely embed in a lambda. We downcast it to the actual Impl type so
    // we can get visitor overload resolution.
    std::shared_ptr<Impl> self = std::static_pointer_cast<Impl>(this->self());

    // Return a FlatMap of the input, where we use absl::visit to dispatch
    // over the input variants.
    return std::move(input).template FlatMap<Output>(
        [self](Input elem) -> rx::Publisher<Output> {
          return absl::visit(*self, std::move(elem));
        });
  }
};

// Example: Coffee Machine

/** Cost per ounce of coffee, in cents. */
constexpr double kCostPerOunce = .25;

/** User inserted a coin. */
struct CoinInserted {
  double value;
};

/** User requests coffee. */
struct CoffeeRequested {
  double ounces;
};

/** Machine dispenses coffee. . */
struct DispenseCoffee {
  double ounces;

  // For test assertions.
  bool operator==(DispenseCoffee const& other) const {
    return ounces == other.ounces;
  }
};

/** Machines signals that more money is needed for dispensing coffee. */
struct NeedMoreMoney {
  double amount;

  // For test assertions.
  bool operator==(NeedMoreMoney const& other) const {
    return amount == other.amount;
  }
};

/** Alias for input type. */
using CoffeeInput = absl::variant<CoinInserted, CoffeeRequested>;

/** Alias for output type. */
using CoffeeOutput = absl::variant<DispenseCoffee, NeedMoreMoney>;

/** The coffee machine class. */
class CoffeeMachine
    : public StateMachine<CoffeeMachine, CoffeeInput, CoffeeOutput> {
 public:
  rx::Publisher<CoffeeOutput> operator()(CoinInserted coin) {
    deposit_ += coin.value;
    return rx::Publisher<CoffeeOutput>();
  }

  rx::Publisher<CoffeeOutput> operator()(CoffeeRequested request) {
    auto cost = request.ounces * kCostPerOunce;
    if (cost > deposit_) {
      return rx::Publisher<CoffeeOutput>(NeedMoreMoney{cost - deposit_});
    }
    deposit_ -= cost;
    return rx::Publisher<CoffeeOutput>(DispenseCoffee{request.ounces});
  }

 private:
  double deposit_{};
};

TEST(Examples, CoffeeMachine) {
  std::shared_ptr<CoffeeMachine> machine = CoffeeMachine::Create();
  std::vector<CoffeeInput> input = {
      CoinInserted{0.25}, CoffeeRequested{12.0}, CoinInserted{11.0 * 0.25},
      CoffeeRequested{12.0}, CoffeeRequested{6.0}};
  std::vector<CoffeeOutput> output;
  machine->Apply(rx::Publisher<CoffeeInput>(input))
      .Subscribe(rx::BufferingSubscriber(output));
  EXPECT_THAT(output, ElementsAre(NeedMoreMoney{11.0 * kCostPerOunce},
                                  DispenseCoffee{12.0},
                                  NeedMoreMoney{6.0 * kCostPerOunce}));
}

// ---------------------------------------------------------------------------
// Extended Finite State Machines
// ---------------------------------------------------------------------------

// This example is a refinement of the StateMachine above, where we use a
// finite set of "state" types to model an (Extended) Finite State Machine (EFSM
// -- https://en.wikipedia.org/wiki/Extended_finite-state_machine). FSMs and
// EFSMs are quite common in describing controllers and network protocols.

/**
 * Base class of finite state machines.
 *
 * Use as in:
 *
 *     class MyMachine :
 *         public FiniteStateMachine<<MyMachine, State, Input, Output> {
 *       public: rx::Publisher<Output> operator()(S..., I...) { ... }
 *     }
 *
 * ... where State = variant<S...>, Input = variant<I...>, and Output arbitrary.
 */
template <typename Impl, typename State, typename Input, typename Output>
class FiniteStateMachine : public rx::SharedObjectImpl<Impl> {
 public:
  rx::Publisher<Output> Apply(rx::Publisher<Input> input) {
    std::shared_ptr<Impl> self = std::static_pointer_cast<Impl>(this->self());
    return std::move(input).template FlatMap<Output>(
        [self](Input elem) -> rx::Publisher<Output> {
          return absl::visit(*self, self->state_, std::move(elem));
        });
  }

  void SwitchTo(State state) { state_ = state; }

 private:
  State state_;
};

// Example

struct Unauthenticated {};
struct Authenticated {};
using AuthState = absl::variant<Unauthenticated, Authenticated>;

struct Login {};
struct Logout {};
struct Privileged {};
using AuthInput = absl::variant<Login, Logout, Privileged>;

struct Success {
  bool operator==(Success const&) const { return true; }
};
struct Failure {
  bool operator==(Failure const&) const { return true; }
};
using AuthOutput = absl::variant<Success, Failure>;

// Note that all of states, inputs, and outputs can in general carry data.
// However, state machine rule selection is purely type based.

class AuthMachine
    : public FiniteStateMachine<AuthMachine, AuthState, AuthInput, AuthOutput> {
 public:
  // We need to provide operator() rules for all combinations of states and
  // inputs, otherwise a compile time error is generated. This could probably
  // be made more convenient by having a default template which catches all
  // illegal inputs.

  // Rules for state Unauthenticated.

  rx::Publisher<AuthOutput> operator()(Unauthenticated, Privileged) {
    return rx::Publisher<AuthOutput>(Failure{});
  }
  rx::Publisher<AuthOutput> operator()(Unauthenticated, Logout) {
    return rx::Publisher<AuthOutput>(Failure{});
  }

  rx::Publisher<AuthOutput> operator()(Unauthenticated, Login) {
    SwitchTo(Authenticated{});
    return rx::Publisher<AuthOutput>(Success{});
  }

  // Rules for state Authenticated

  rx::Publisher<AuthOutput> operator()(Authenticated, Privileged) {
    return rx::Publisher<AuthOutput>(Success{});
  }
  rx::Publisher<AuthOutput> operator()(Authenticated, Logout) {
    SwitchTo(Unauthenticated{});
    return rx::Publisher<AuthOutput>(Success{});
  }
  rx::Publisher<AuthOutput> operator()(Authenticated, Login) {
    return rx::Publisher<AuthOutput>(Failure{});
  }
};

TEST(Examples, AuthMachine) {
  std::shared_ptr<AuthMachine> machine = AuthMachine::Create();
  std::vector<AuthInput> input = {Logout{},     Privileged{}, Login{},
                                  Privileged{}, Login{},      Logout{},
                                  Privileged{}};
  std::vector<AuthOutput> output;
  machine->Apply(rx::Publisher<AuthInput>(input))
      .Subscribe(rx::BufferingSubscriber(output));
  EXPECT_THAT(output, ElementsAre(Failure{}, Failure{}, Success{}, Success{},
                                  Failure{}, Success{}, Failure{}));
}

// ---------------------------------------------------------------------------
// Async
// ---------------------------------------------------------------------------

// This example illustrates usage of the Async operator which allows
// to produce and consume streams on different threads.
//
// Also see the Activity example later on for more sophisticated usage of this
// operator.

TEST(Examples, Async) {
  // Creates a scheduler and two workers, one for producing elements
  // and one for consuming. Each worker will guarantee sequential execution
  // whereas different workers act concurrently.
  auto scheduler = rx::CreateThreadPoolScheduler(2);
  auto produce_on = std::shared_ptr<rx::Worker>(scheduler->CreateWorker());
  auto consume_on = std::shared_ptr<rx::Worker>(scheduler->CreateWorker());

  // Creates a publisher which pretends to do hard work.
  auto pub = rx::Publisher<int>({1, 2, 3}).Map<int>([](int x) {
    absl::SleepFor(absl::Milliseconds(200));
    return x + 1;
  });

  // Setup publisher to be produced on one worker and consumed on another.
  pub = std::move(pub).Async(produce_on, consume_on);

  // Async subscription immediately returns, with the processing happening in
  // the assigned workers and underlying threads. Streams will destroy
  // themselves once they are done with their work and are decoupled from this
  // method's execution scope. We need to wait until all tasks in our
  // scheduler have finished before verifying the result.
  std::vector<int> output;
  std::move(pub).Subscribe(rx::BufferingSubscriber(output));
  scheduler->WaitUntilIdle();
  ASSERT_THAT(output, ElementsAre(2, 3, 4));
}

// --------------------------------------------------------------------------
// Activity
// --------------------------------------------------------------------------

// This example illustrates the usage of the Async operator to create
// higher-level abstractions for asynchronous programming.
//
// The concept of an *Activity* is introduced. Each Activity represents
// a sequential execution flow (based on an rx::Worker). Multiple activities
// can be created which operate concurrently.
//
// Activities offer one public method which allows another activity to request
// provisioning a stream processor on the activity. (A stream processor is a
// function from publisher to publisher). Provisioned processors will receive
// inputs from the requesting activity, perform the processing step on the given
// activity, and send outputs back to the requesting activity. Because of
// the sequential execution contract of workers/activities, this enables a
// lock-free asynchronous programming model.
//
// Usage of activities is illustrated by a canonical map-reduce problem (word
// counting).

/**
 * The class of activities. An Activity implements the rx::Worker interface
 * which allows to schedule tasks on it and pass it into the Async operator.
 * Activities maintain the notion of the currently running Activity in a
 * thread_local, enabling the implementation of the primary Provide() method.
 */
class Activity : public rx::Worker, public rx::SharedObjectImpl<Activity> {
 public:
  explicit Activity(std::shared_ptr<rx::Worker> const& worker)
      : base_worker_(worker) {}
  explicit Activity(std::unique_ptr<rx::Worker> worker)
      : base_worker_(std::shared_ptr<rx::Worker>(std::move(worker))) {}

  /**
   * Returns the current activity.
   */
  static std::shared_ptr<Activity> current() {
    // thread-safe because current_activity is thread-local.
    RX_CHECK(current_) << "no current Activity";
    return current_;
  }

  /**
   * Implements Worker::Schedule()
   */
  void Schedule(std::function<void()> task) override {
    auto wrapped_task = rx::MoveToLambda(std::move(task));
    auto self = this->self();
    base_worker_->Schedule([self, wrapped_task] {
      RX_CHECK(!current_) << "stale current Activity";
      current_ = self;
      (*wrapped_task)();
      current_.reset();
    });
  }

  /**
   * Provides a stream processor on this activity for the current activity.
   *
   * A stream processor is a function<Publisher<O>(Publisher<I>)> which takes
   * an input stream and produces an output stream. This call returns a new
   * processor which will receive inputs from the currently executing activity,
   * execute the processor on the providing activity, and return the results
   * back to the current activity.
   */
  template <typename I, typename O>
  rx::Processor<I, O> Provide(rx::Processor<I, O> processor) {
    auto caller = Activity::current();
    auto self = this->self();
    auto wrapped_processor = rx::MoveToLambda(std::move(processor));
    return [wrapped_processor, caller, self](rx::Publisher<I> input) {
      // The two-liner below builds a pipeline as such:
      //
      //   caller                               self
      //     |
      //     ------------- input ----------------->
      //     .                                    |
      //     .                                processor()
      //     .                                    |
      //     <------------ output -----------------
      //
      return (*wrapped_processor)(std::move(input).Async(caller, self))
          .Async(self, caller);
    };
  }

 private:
  static thread_local std::shared_ptr<Activity> current_;
  std::shared_ptr<Worker> const base_worker_;
};

thread_local std::shared_ptr<Activity> Activity::current_;

// Example: MapReduce

/**
 * A processor which counts words in a stream of documents. For each
 * document, it returns a multiset with the word counts.
 */
rx::Publisher<std::multiset<std::string>> CountWords(
    rx::Publisher<std::string> docs) {
  return std::move(docs).Map<std::multiset<std::string>>([](std::string doc) {
    return std::multiset<std::string>(
        absl::StrSplit(doc, ' ', absl::SkipWhitespace()));
  });
}

TEST(Examples, CountWordsMapReduce) {
  // Create a scheduler and an activity which will spawn mappers and reduce
  // their output.
  auto scheduler = rx::CreateThreadPoolScheduler(4);
  auto reducer = Activity::Create(scheduler->CreateWorker());

  std::multiset<std::string> accumulated;
  reducer->Schedule([&scheduler, &accumulated] {
    for (int i = 0; i < 3; ++i) {
      // Create a mapper and provide the CountWords processor on it.
      auto mapper = Activity::Create(scheduler->CreateWorker());
      auto count_words_processor =
          mapper->Provide<std::string, std::multiset<std::string>>(CountWords);

      // Subscribe to the processor, sending some example docs to it.
      // The processor will be executed on the mappers worker, while the
      // Subscribe will receive the result on the reducers worker. Because
      // of this, we don't need to be concerned about racing conditions in
      // Subscribe. Even though the mappers work concurrently, processing their
      // results will happen sequentially.
      std::vector<std::string> docs = {"a b c", "d e f", "g"};
      count_words_processor(rx::Publisher<std::string>(docs))
          .Subscribe(
              [&accumulated](rx::StatusOr<std::multiset<std::string>> words) {
                if (!rx::IsEnd(words)) {
                  accumulated.insert(words.ValueOrDie().begin(),
                                     words.ValueOrDie().end());
                }
              });
    }
  });

  // Wait until all work is done.
  scheduler->WaitUntilIdle();

  // Verify the results. Since we send the same docs to all 3 mappers, we
  // expect each word to appear 3 times.
  for (std::string word : {"a", "b", "c", "d", "e", "f", "g"}) {
    ASSERT_EQ(accumulated.count(word), 3);
  }
}

}  // namespace
}  // namespace rx_example
