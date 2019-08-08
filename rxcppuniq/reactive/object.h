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

#ifndef RXCPPUNIQ_REACTIVE_OBJECT_H_
#define RXCPPUNIQ_REACTIVE_OBJECT_H_

/**
 * Overview
 * ========
 *
 * This file introduces helper classes for dealing with memory managed objects,
 * as well as some general utilities which are related to memory.
 */

#include <memory>

#include "absl/memory/memory.h"
#include "rxcppuniq/base/monitoring.h"

namespace rx {

/**
 * Base class for values wrapping a shared pointer to an implementation.
 *
 * One can inherit constructors for a derived class using the following
 * pattern:
 *
 *   class Something : public SharedObject<SomeImpl> {
 *    public:
 *     using SharedObject<SomeImpl>::SharedObject;
 *     ...
 *
 * This behaves semantically equivalent to a std::shared_ptr<Impl>, but
 * adds some utilities (and is is a future extension point for such).
 */
template <typename Impl>
class SharedObject {
 public:
  /**
   * The implementation type of this object.
   */
  using ImplType = Impl;

  /**
   * Creates an empty object with no implementation pointer.
   */
  inline SharedObject() : impl_() {}

  /**
   * Construct an object from a std::shared_ptr to the implementation. Implicit
   * construction allowed because this is always safe.
   */
  inline SharedObject(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

  /**
   * Access the implementation, checked.
   */
  inline const std::shared_ptr<Impl>& impl() const {
    FCP_CHECK(impl_) << "implementation missing";
    return impl_;
  }

  /**
   * Cast the object into a new type using static_pointer_cast on the underlying
   * representation.
   */
  template <typename NewImpl>
  inline SharedObject<NewImpl> Cast() {
    return SharedObject<NewImpl>(std::static_pointer_cast<NewImpl>(impl_));
  }

  /**
   * Forwards operator-> to the members of the implementation.
   */
  inline Impl* operator->() const { return impl().get(); }

  /**
   * Forwards get() to underlying shared_ptr.
   */
  inline Impl* get() const { return impl_.get(); }

  /**
   * Forwards reset() to underlying shared_ptr.
   */
  inline void reset() { impl_.reset(); }

  /**
   * Forwards operator bool(). Returns true if the implementation is set.
   */
  explicit operator bool() const { return static_cast<bool>(impl_); }

  bool operator==(SharedObject<Impl> const& other) const {
    return impl_ == other.impl_;
  }
  bool operator!=(SharedObject<Impl> const& other) const {
    return impl_ != other.impl_;
  }
  bool operator<(SharedObject<Impl> const& other) const {
    return impl_ < other.impl_;
  }
  bool operator<=(SharedObject<Impl> const& other) const {
    return impl_ <= other.impl_;
  }

 private:
  std::shared_ptr<Impl> impl_;
};

/*
 * Base class which can be used for the implementation class in
 * SharedObject<Impl>. This provides access to a shared pointer to this object
 * (self()), and a factory for creating instances.
 *
 * The self() pointer is useful if the implementation needs to pass a pointer to
 * itself to other methods or capture it in lambdas.
 */
template <typename Impl>
class SharedObjectImpl {
 public:
  using ImplType = Impl;

  SharedObjectImpl() = default;

  // Delete copy and assign, as this object cannot be copied.
  SharedObjectImpl(const SharedObjectImpl&) = delete;
  SharedObjectImpl& operator=(const SharedObjectImpl&) = delete;

  /**
   * Create an instance of this implementation as a std::shared_ptr, setting up
   * the self() pointer.
   */
  template <typename... TS>
  static inline std::shared_ptr<Impl> Create(TS&&... arg) {
    auto self = std::make_shared<Impl>(std::forward<TS>(arg)...);
    self->self_ = self;
    return self;
  }

  /**
   * Returns a std::shared_ptr for this object which is sharing ownership
   * with other objects descending from a call to Create. The implementation
   * is based on std::weak_ptr and this returns the result of weak_ptr::lock.
   *
   * See class documentation for when to use self().
   */
  std::shared_ptr<Impl> self() const {
    FCP_CHECK(!self_.expired()) << "lifetime expired";
    return self_.lock();
  }

 private:
  std::weak_ptr<Impl> self_{};
};

/**
 * Base class for values wrapping a unique pointer to an implementation.
 *
 * This behaves semantically equivalent to a std::unique_ptr<Impl>, but
 * adds some utilities (and is is a future extension point for such).
 */
template <typename Impl>
class UniqueObject {
 public:
  /**
   * The implementation type of this object.
   */
  using ImplType = Impl;

  /**
   * Creates an empty object with no implementation pointer.
   */
  inline UniqueObject() : impl_() {}

  /**
   * Construct an object from a std::unique_ptr to the implementation. Implicit
   * construction allowed because this is always safe.
   */
  inline UniqueObject(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

  /**
   * Access the implementation, checked.
   */
  inline std::unique_ptr<Impl>& impl() {
    FCP_CHECK(impl_) << "implementation missing (maybe moved)";
    return impl_;
  }

  /**
   * Move the implementation out of this object. It will be afterwards
   * undefined.
   */
  inline std::unique_ptr<Impl> move_impl() { return std::move(impl()); }

  /**
   * Cast the object into a new type using static_pointer_cast on the underlying
   * representation.
   */
  template <typename NewImpl>
  inline UniqueObject<NewImpl> Cast() && {
    Impl* tmp = impl_.release();
    return UniqueObject<NewImpl>(
        std::unique_ptr<NewImpl>(static_cast<NewImpl*>(tmp)));
  }

  /**
   * Forwards operator-> to the members of the implementation.
   */
  inline Impl* operator->() const { return impl_.get(); }

  /**
   * Forwards get() to underlying unique_ptr.
   */
  inline Impl* get() const { return impl_.get(); }

  /**
   * Forwards reset() to underlying unique_ptr.
   */
  inline void reset() { impl_.reset(); }

  /**
   * Forwards operator bool(). Returns true if the implementation is set.
   */
  explicit operator bool() const { return static_cast<bool>(impl_); }

  bool operator==(UniqueObject<Impl> const& other) const {
    return impl_ == other.impl_;
  }
  bool operator!=(UniqueObject<Impl> const& other) const {
    return impl_ != other.impl_;
  }
  bool operator<(UniqueObject<Impl> const& other) const {
    return impl_ < other.impl_;
  }
  bool operator<=(UniqueObject<Impl> const& other) const {
    return impl_ <= other.impl_;
  }

 private:
  std::unique_ptr<Impl> impl_;
};

/*
 * Base class which can be used for the implementation class in
 * UniqueObject<Impl>. This provides a factory for creating instances.
 */
template <typename Impl>
class UniqueObjectImpl {
 public:
  using ImplType = Impl;

  UniqueObjectImpl() = default;

  // Delete copy and assign, as this object cannot be copied.
  UniqueObjectImpl(const UniqueObjectImpl&) = delete;
  UniqueObjectImpl& operator=(const UniqueObjectImpl&) = delete;

  /**
   * Create an instance of this implementation as a std::unique_ptr.
   */
  template <typename... TS>
  static inline std::unique_ptr<Impl> Create(TS&&... arg) {
    return absl::make_unique<Impl>(std::forward<TS>(arg)...);
  }
};

}  // namespace rx

#endif  // RXCPPUNIQ_REACTIVE_OBJECT_H_
