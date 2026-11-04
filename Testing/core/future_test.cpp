#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "ara/core/core_error_domain.h"
#include "ara/core/future.h"

using namespace ara::core;
using namespace std::chrono_literals;

TEST(Future, ValueSetBeforeGet) {
  Promise<int> p;
  auto f = p.get_future();
  EXPECT_TRUE(f.valid());
  EXPECT_FALSE(f.is_ready());
  p.set_value(7);
  EXPECT_TRUE(f.is_ready());
  EXPECT_EQ(f.get(), 7);
  EXPECT_FALSE(f.valid());
}

TEST(Future, ErrorIsReportedViaResult) {
  Promise<int> p;
  auto f = p.get_future();
  p.SetError(MakeErrorCode(CoreErrc::kInvalidArgument));
  auto r = f.GetResult();
  EXPECT_FALSE(r.HasValue());
  EXPECT_TRUE(r.CheckError(CoreErrc::kInvalidArgument));
}

TEST(Future, GetThrowsOnError) {
  Promise<int> p;
  auto f = p.get_future();
  p.SetError(MakeErrorCode(CoreErrc::kInvalidArgument));
  EXPECT_THROW((void)f.get(), CoreException);
}

TEST(Future, WaitForTimesOutThenSucceeds) {
  Promise<int> p;
  auto f = p.get_future();
  EXPECT_EQ(f.wait_for(10ms), future_status::timeout);
  std::thread t([&] {
    std::this_thread::sleep_for(20ms);
    p.set_value(1);
  });
  EXPECT_EQ(f.wait_for(2s), future_status::ready);
  EXPECT_EQ(f.get(), 1);
  t.join();
}

TEST(Future, SecondGetFutureIsInvalid) {
  Promise<int> p;
  auto f1 = p.get_future();
  auto f2 = p.get_future();
  EXPECT_TRUE(f1.valid());
  EXPECT_FALSE(f2.valid());
}

TEST(Future, OnlyFirstSatisfactionWins) {
  Promise<int> p;
  auto f = p.get_future();
  p.set_value(1);
  p.set_value(2);
  p.SetError(MakeErrorCode(CoreErrc::kInvalidArgument));
  EXPECT_EQ(f.get(), 1);
}

TEST(Future, VoidFuture) {
  Promise<void> p;
  auto f = p.get_future();
  p.set_value();
  EXPECT_NO_THROW(f.get());
}

TEST(Future, ThenRunsImmediatelyWhenReady) {
  Promise<int> p;
  auto f = p.get_future();
  p.set_value(20);
  auto next = f.then([](Future<int> ready) { return ready.get() + 1; });
  EXPECT_EQ(next.get(), 21);
}

TEST(Future, ThenRunsOnFulfilmentAndChains) {
  Promise<int> p;
  std::atomic<bool> ran{false};
  auto next = p.get_future()
                  .then([&](Future<int> ready) {
                    ran = true;
                    return ready.get() * 2;
                  })
                  .then([](Future<int> ready) { return std::to_string(ready.get()); });
  EXPECT_FALSE(ran);
  p.set_value(21);
  EXPECT_TRUE(ran);
  EXPECT_EQ(next.get(), "42");
}

TEST(Future, ThenPropagatesResultErrors) {
  Promise<int> p;
  auto next = p.get_future().then([](Future<int> ready) -> Result<int> {
    auto r = ready.GetResult();
    if (!r.HasValue()) {
      return Result<int>(r.Error());
    }
    return Result<int>(r.Value());
  });
  p.SetError(MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
  EXPECT_TRUE(next.GetResult().CheckError(CoreErrc::kInvalidMetaModelPath));
}

TEST(Future, ManyProducersAndConsumers) {
  constexpr int N = 200;
  std::vector<Promise<int>> promises(N);
  std::vector<Future<int>> futures;
  for (auto &p : promises) {
    futures.push_back(p.get_future());
  }
  std::thread producer([&] {
    for (int i = 0; i < N; ++i) {
      promises[static_cast<std::size_t>(i)].set_value(i);
    }
  });
  int sum = 0;
  for (auto &f : futures) {
    sum += f.get();
  }
  producer.join();
  EXPECT_EQ(sum, N * (N - 1) / 2);
}
