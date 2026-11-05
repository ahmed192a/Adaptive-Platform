// Tests for the items fixed during the ara::core spec-conformance audit
// (see doc/spec-conformance/core.md).
#include <gtest/gtest.h>

#include <cstdio>
#include <functional>
#include <new>
#include <string>
#include <vector>

#include "ara/core/abort.h"
#include "ara/core/core_error_domain.h"
#include "ara/core/future.h"
#include "ara/core/future_error_domain.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/memory_resource.h"
#include "ara/core/result.h"
#include "ara/core/variant.h"

using namespace ara::core;

// ---- Exception ---------------------------------------------------------------
TEST(SpecException, WhatReturnsErrorMessage) {
  CoreException const e(MakeErrorCode(CoreErrc::kInvalidArgument));
  EXPECT_STREQ(e.what(), MakeErrorCode(CoreErrc::kInvalidArgument).Message());
  Exception const &base = e;
  EXPECT_STREQ(base.what(), "an invalid argument was passed to a function");
}

TEST(SpecException, ConstructibleFromLvalueErrorCode) {
  ErrorCode const ec = MakeErrorCode(CoreErrc::kInvalidMetaModelPath);
  FutureException const e(ec); // by-value constructor accepts lvalues
  EXPECT_EQ(e.Error(), ec);
}

// ---- Future error domain -------------------------------------------------------
TEST(SpecFutureDomain, IdNameAndCodes) {
  EXPECT_EQ(GetFutureDomain().Id(), 0x8000000000000013ULL); // SWS_CORE_00013
  EXPECT_EQ(GetCoreErrorDomain().Id(), 0x8000000000000014ULL); // SWS_CORE_00014
  EXPECT_STREQ(GetFutureDomain().Name(), "Future");
  EXPECT_NE(GetFutureDomain(), GetCoreErrorDomain());
  for (auto c : {FutureErrc::broken_promise, FutureErrc::future_already_retrieved,
                 FutureErrc::promise_already_satisfied, FutureErrc::no_state}) {
    EXPECT_STRNE(MakeErrorCode(c).Message(), "unknown future error");
    EXPECT_THROW(MakeErrorCode(c).ThrowAsException(), FutureException);
  }
}

// ---- Promise / Future semantics ------------------------------------------------
TEST(SpecFuture, BrokenPromiseIsDeliveredToFuture) {
  Future<int> f;
  {
    Promise<int> p;
    f = p.get_future();
  } // promise destroyed unfulfilled
  ASSERT_TRUE(f.is_ready());
  auto r = f.GetResult();
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(FutureErrc::broken_promise));
}

TEST(SpecFuture, BrokenPromiseWakesBlockedWaiter) {
  Promise<void> p;
  auto f = p.get_future();
  p.~Promise();
  new (&p) Promise<void>(); // re-arm storage so the implicit destructor stays valid
  EXPECT_EQ(f.wait_for(std::chrono::seconds(1)), future_status::ready);
  EXPECT_FALSE(f.GetResult().HasValue());
}

TEST(SpecFuture, FulfilledPromiseIsNotBroken) {
  Future<int> f;
  {
    Promise<int> p;
    f = p.get_future();
    p.set_value(5);
  }
  EXPECT_EQ(f.get(), 5);
}

TEST(SpecFuture, MovedFromPromiseDoesNotBreakTheFuture) {
  Promise<int> a;
  auto f = a.get_future();
  Promise<int> b(std::move(a));
  EXPECT_FALSE(f.is_ready());
  b.set_value(9);
  EXPECT_EQ(f.get(), 9);
  EXPECT_THROW(a.set_value(1), FutureException); // moved-from: no_state
}

TEST(SpecFuture, MoveAssignAbandonsTheOverwrittenState) {
  Promise<int> a;
  auto fa = a.get_future();
  Promise<int> b;
  a = std::move(b); // old state of `a` is abandoned
  ASSERT_TRUE(fa.is_ready());
  EXPECT_EQ(fa.GetResult().Error(), MakeErrorCode(FutureErrc::broken_promise));
}

TEST(SpecFuture, InvalidFutureReportsNoState) {
  Future<int> f;
  EXPECT_FALSE(f.valid());
  EXPECT_FALSE(f.is_ready());
  for (auto const &fn : std::vector<std::function<void()>>{
           [&] { f.wait(); },
           [&] { (void)f.wait_for(std::chrono::milliseconds(1)); },
           [&] { (void)f.GetResult(); },
           [&] { (void)f.get(); }}) {
    try {
      fn();
      FAIL() << "expected FutureException";
    } catch (FutureException const &e) {
      EXPECT_EQ(e.Error(), MakeErrorCode(FutureErrc::no_state));
    }
  }
}

TEST(SpecFuture, GetResultInvalidatesTheFuture) {
  Promise<int> p;
  auto f = p.get_future();
  p.set_value(1);
  EXPECT_TRUE(f.GetResult().HasValue());
  EXPECT_FALSE(f.valid());
  EXPECT_THROW(f.wait(), FutureException);
}

namespace {
struct PlainError {
  int code;
};
} // namespace

TEST(SpecFuture, ErrorChannelOtherThanErrorCodeKeepsFuturePending) {
  // E not constructible from ErrorCode => no broken_promise can be expressed.
  Future<int, PlainError> f;
  {
    Promise<int, PlainError> p;
    f = p.get_future();
  }
  EXPECT_FALSE(f.is_ready());
}

// ---- Result factories use direct-initialization ----------------------------------
TEST(SpecResult, FromValueUsesParenthesesNotBraces) {
  auto r = Result<std::vector<int>>::FromValue(3, 7); // vector(3, 7) -> {7,7,7}
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value(), (std::vector<int>{7, 7, 7}));
  auto s = Result<std::string>::FromValue(4, 'x');
  EXPECT_EQ(s.Value(), "xxxx");
}

TEST(SpecResult, FromValueStillWorksForAggregates) {
  struct Pt {
    int x;
    int y;
  };
  auto r = Result<Pt>::FromValue(1, 2);
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(r.Value().x, 1);
  EXPECT_EQ(r.Value().y, 2);
}

TEST(SpecResult, FromErrorForwardsConstructorArguments) {
  auto r = Result<int>::FromError(CoreErrc::kInvalidArgument);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(CoreErrc::kInvalidArgument));
  auto v = Result<void>::FromError(CoreErrc::kInvalidMetaModelPath);
  EXPECT_EQ(v.Error(), MakeErrorCode(CoreErrc::kInvalidMetaModelPath));
}

// ---- InstanceSpecifier ---------------------------------------------------------------
TEST(SpecInstanceSpecifier, SymmetricStringViewComparison) {
  auto is = InstanceSpecifier::Create("App/Port");
  ASSERT_TRUE(is.HasValue());
  StringView const sv = "App/Port";
  EXPECT_TRUE(sv == is.Value());
  EXPECT_TRUE(is.Value() == sv);
  EXPECT_FALSE(sv != is.Value());
  EXPECT_TRUE(StringView("Other") != is.Value());
}

// ---- Variant helpers ---------------------------------------------------------------------
TEST(SpecVariant, TraitsAndBadAccess) {
  using V = Variant<int, double, monostate>;
  static_assert(variant_size_v<V> == 3);
  static_assert(std::is_same_v<variant_alternative_t<1, V>, double>);
  EXPECT_EQ(variant_size<V>::value, 3u);
  V v = 1.5;
  EXPECT_THROW((void)get<int>(v), bad_variant_access);
  EXPECT_NE(variant_npos, v.index());
}

// ---- Abort -----------------------------------------------------------------------------
namespace {
int gHandlerCalls = 0;
void CountingHandler() noexcept { ++gHandlerCalls; }
} // namespace

TEST(SpecAbort, SetAbortHandlerReturnsPrevious) {
  EXPECT_EQ(SetAbortHandler(&CountingHandler), nullptr);
  EXPECT_EQ(SetAbortHandler(nullptr), &CountingHandler);
}

TEST(SpecAbortDeathTest, AbortRunsHandlerThenTerminates) {
  EXPECT_DEATH(
      {
        SetAbortHandler([]() noexcept { std::fputs("handler-ran\n", stderr); });
        Abort("fatal condition");
      },
      "fatal condition.*handler-ran");
}

// ---- MemoryResource ----------------------------------------------------------------------
TEST(SpecMemoryResource, DefaultResourceAccessors) {
  MemoryResource *orig = GetDefaultResource();
  ASSERT_NE(orig, nullptr);
  MemoryResource *prev = SetDefaultResource(NullMemoryResource());
  EXPECT_EQ(prev, orig);
  EXPECT_EQ(GetDefaultResource(), NullMemoryResource());
  SetDefaultResource(nullptr); // restores new_delete_resource
  EXPECT_EQ(GetDefaultResource(), NewDeleteResource());
}
