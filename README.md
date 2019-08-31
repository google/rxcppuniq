# Introduction

This repository contains an implementation of
[reactive streams](http://www.reactive-streams.org) in C++.

Reactive streams are an event driven, flow controlled communication mechanism.
They have been derived from the work around
[Reactive Extensions](http://reactivex.io/) (ReactiveX or RX for short), and are
implemented in many languages.

The implementation provided here is designed around the concept of *unique
types* in C++ -- types with move semantics. Stream elements are produced and
moved through a stream processing pipeline, with unique ownership maintained.
Pipelines themselves are also unique, having fully automated memory management.
Pipelines can be produced and consumed asynchronously. The design is using
strict flow control, that is, a consumer does not receive elements until he has
requested them.

This is not an officially supported Google product.

# Usage

Reactive streams do not appear explicitly as types (there is no 'stream' type).
Rather, the type `Publisher<T>` is used to represent a *stream builder*. A
receiver must *subscribe* to a Publisher<T>, at which point of time the actual
stream will be created under the hood.

Various operators are available for publishers which allow to create processing
pipelines. Here is an example:

```C++
rx::Publisher<int>({1, 2, 3})
   .Map([](int x) { return x*2; })
   .Buffer(2)  // batch in sizes of 2
   .Subscribe(
     // on_subscribe callback: this is called once at the beginning
     // of a stream lifetime.
     [](rx::Subscription const& subscription) {
       // Request the first element.
       subscription.Request(1);
     },
     // on_next callback: this is called each time a new element
     // arrives on the stream, or when the end of the stream is
     // reached or an error occured, as indicated by the StatusOr.
     [](rx::Subscription const& subscription, rx::StatusOr<std::vector<int>> v) {
       if (rx::IsEnd(v)) {
         std::cout << "end" << std::endl;
       } else {
         std::cout << v.ValueOrDie() << std::endl;
         // Request the next element.
         subscription.Request(1);
       }
     });

Prints:
  {2, 4}
  {6}
  end
```

Publishers are unique values, which do not have a copy constructor, but can only
be moved. Moreover, in a pipeline like above, each method call consumes the
previous instance by having the underlying resources of a publisher moved into
the newly created publisher. A publisher is finally moved into the
subscription/stream created by the Subscribe method. A stream's lifetime is
self-determined: it (and the resources associated with it) live as long as the
end-of-stream marker has been send, or the subscriber has cancelled its
subscription.

More specifically, the above is equivalent to:

```C++
auto p = rx::Publisher<int>({1, 2, 3});
p = std::move(p).Map(...);
p = std::move(p).Buffer(...);
std::move(p).Subscribe(...);
```

Note that the model of reactive streams here differs slightly from the one found
e.g. in Java and other languages specifically:

1.  Each publisher can have at most one subscriber, which is expressed via move
    semantics of a Publisher.
2.  As seen in the above example, we use `StatusOr<T>` as a generalized event
    notification instead of separate methods for next value, error, and
    termination. One reason for this is to support C++ move semantics better. We
    cannot move a unique context object required for processing into three
    different lambdas for handling, therefore we use only one.

For more information, see the collection of [examples](rxcppuniq/example/examples.cc).

# Contributions

This project is in an early stage. Contributions are welcome. Please see
[CONTRIBUTING.md](CONTRIBUTING.md) for more information.

