#include <gtest/gtest.h>

#include <string>

#include "ara/core/core_error_domain.h"
#include "ara/core/result.h"

using ara::core::CoreErrc;
using ara::core::ErrorCode;
using ara::core::MakeErrorCode;
using ara::core::Result;

namespace {
ErrorCode Err() { return MakeErrorCode(CoreErrc::kInvalidArgument); }
} // namespace

TEST(Result, HoldsValue) {
  Result<int> r(42);
  ASSERT_TRUE(r.HasValue());
  EXPECT_TRUE(static_cast<bool>(r));
  EXPECT_EQ(r.Value(), 42);
  EXPECT_EQ(*r, 42);
  EXPECT_EQ(r.Ok().value(), 42);
  EXPECT_FALSE(r.Err().has_value());
}

TEST(Result, HoldsError) {
  Result<int> r(Err());
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), Err());
  EXPECT_FALSE(r.Ok().has_value());
  EXPECT_TRUE(r.Err().has_value());
  EXPECT_TRUE(r.CheckError(CoreErrc::kInvalidArgument));
  EXPECT_FALSE(r.CheckError(CoreErrc::kInvalidMetaModelPath));
}

TEST(Result, FactoryFunctions) {
  auto v = Result<std::string>::FromValue("abc");
  auto e = Result<std::string>::FromError(CoreErrc::kInvalidMetaModelPath);
  EXPECT_EQ(v.Value(), "abc");
  EXPECT_TRUE(e.CheckError(CoreErrc::kInvalidMetaModelPath));
}

TEST(Result, ValueOrAndErrorOr) {
  Result<int> ok(7);
  Result<int> bad(Err());
  EXPECT_EQ(ok.ValueOr(99), 7);
  EXPECT_EQ(bad.ValueOr(99), 99);
  EXPECT_EQ(ok.ErrorOr(MakeErrorCode(CoreErrc::kInvalidMetaModelShortname)),
            MakeErrorCode(CoreErrc::kInvalidMetaModelShortname));
  EXPECT_EQ(bad.ErrorOr(MakeErrorCode(CoreErrc::kInvalidMetaModelShortname)), Err());
}

TEST(Result, Resolve) {
  Result<int> bad(Err());
  EXPECT_EQ(bad.Resolve([](ErrorCode const &) { return -1; }), -1);
  EXPECT_EQ(Result<int>(5).Resolve([](ErrorCode const &) { return -1; }), 5);
}

TEST(Result, BindPlainAndNested) {
  Result<int> ok(10);
  auto doubled = ok.Bind([](int v) { return v * 2; });
  EXPECT_EQ(doubled.Value(), 20);

  auto chained = ok.Bind([](int v) -> Result<std::string> { return std::to_string(v); });
  EXPECT_EQ(chained.Value(), "10");

  Result<int> bad(Err());
  auto propagated = bad.Bind([](int v) { return v * 2; });
  ASSERT_FALSE(propagated.HasValue());
  EXPECT_EQ(propagated.Error(), Err());
}

TEST(Result, VoidSpecialization) {
  Result<void> ok;
  Result<void> bad(Err());
  EXPECT_TRUE(ok.HasValue());
  EXPECT_FALSE(bad.HasValue());
  EXPECT_EQ(bad.Error(), Err());
  EXPECT_TRUE(bad.CheckError(CoreErrc::kInvalidArgument));
  auto after = ok.Bind([] { return 3; });
  EXPECT_EQ(after.Value(), 3);
}

TEST(Result, EmplaceAndSwap) {
  Result<int> a(1);
  Result<int> b(Err());
  a.Swap(b);
  EXPECT_FALSE(a.HasValue());
  EXPECT_EQ(b.Value(), 1);
  a.EmplaceValue(5);
  EXPECT_EQ(a.Value(), 5);
  a.EmplaceError(CoreErrc::kInvalidMetaModelPath);
  EXPECT_TRUE(a.CheckError(CoreErrc::kInvalidMetaModelPath));
}

TEST(Result, Comparison) {
  EXPECT_EQ(Result<int>(3), Result<int>(3));
  EXPECT_NE(Result<int>(3), Result<int>(4));
  EXPECT_NE(Result<int>(3), Result<int>(Err()));
  EXPECT_TRUE(Result<int>(3) == 3);
  EXPECT_TRUE(3 == Result<int>(3));
  EXPECT_TRUE(Result<int>(Err()) == Err());
  EXPECT_EQ(Result<void>(), Result<void>());
}

TEST(Result, ValueOrThrowThrowsCoreException) {
  Result<int> bad(Err());
  EXPECT_THROW((void)bad.ValueOrThrow(), ara::core::CoreException);
  Result<int> ok(1);
  EXPECT_EQ(ok.ValueOrThrow(), 1);
  Result<void> badVoid(Err());
  EXPECT_THROW(badVoid.ValueOrThrow(), ara::core::CoreException);
}

TEST(Result, MoveOnlyValue) {
  Result<std::unique_ptr<int>> r(std::make_unique<int>(9));
  auto moved = std::move(r).Value();
  EXPECT_EQ(*moved, 9);
}
