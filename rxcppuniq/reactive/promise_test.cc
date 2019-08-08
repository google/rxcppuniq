#include "rxcppuniq/reactive/promise.h"

#include <memory>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rxcppuniq/base/monitoring.h"
#include "rxcppuniq/base/testing.h"

namespace rx {
namespace {

TEST(Promise, Fulfill) {
  Promise<int> p{22};
  std::move(p).Subscribe([](StatusOr<int> result) {
    EXPECT_THAT(result, IsOk());
    EXPECT_EQ(result.ValueOrDie(), 22);
  });
}

TEST(Promise, Error) {
  Promise<int> p = Promise<int>::Error(FCP_STATUS(INVALID_ARGUMENT));
  std::move(p).Subscribe([](StatusOr<int> result) {
    EXPECT_THAT(result.status(), IsCode(INVALID_ARGUMENT));
  });
}

TEST(Promise, ContinueWith) {
  Promise<int> p{1};
  std::move(p)
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .Subscribe([](StatusOr<int> result) {
        EXPECT_THAT(result, IsOk());
        EXPECT_EQ(result.ValueOrDie(), 3);
      });
}

TEST(Promise, ContinueWithErrors) {
  Promise<int> p{1};
  std::move(p)
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .ContinueWith<int>(
          [](int x) { return Promise<int>::Error(FCP_STATUS(INTERNAL)); })
      .ContinueWith<int>([](int x) { return Promise<int>(x + 1); })
      .Subscribe(
          [](StatusOr<int> result) { EXPECT_THAT(result, IsCode(INTERNAL)); });
}

}  // namespace
}  // namespace rx
