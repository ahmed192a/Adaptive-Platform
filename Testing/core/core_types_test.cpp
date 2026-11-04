#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <thread>
#include <unordered_set>

#include "ara/core/array.h"
#include "ara/core/byte.h"
#include "ara/core/core_error_domain.h"
#include "ara/core/initialization.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/map.h"
#include "ara/core/optional.h"
#include "ara/core/steady_clock.h"
#include "ara/core/string.h"
#include "ara/core/variant.h"
#include "ara/core/vector.h"
#include "ara/exec/exec_error_domain.h"

using namespace ara::core;

// ---- error domains ---------------------------------------------------------
TEST(CoreErrorDomain, CodesMatchSpecification) {
  EXPECT_EQ(static_cast<int>(CoreErrc::kInvalidArgument), 22);
  EXPECT_EQ(static_cast<int>(CoreErrc::kInvalidMetaModelShortname), 137);
  EXPECT_EQ(static_cast<int>(CoreErrc::kInvalidMetaModelPath), 138);
}

TEST(CoreErrorDomain, NameMessageAndThrow) {
  ErrorCode const ec = MakeErrorCode(CoreErrc::kInvalidArgument);
  EXPECT_STREQ(ec.Domain().Name(), "Core");
  EXPECT_NE(std::string(ec.Message()).find("invalid argument"), std::string::npos);
  EXPECT_THROW(ec.ThrowAsException(), CoreException);
  try {
    ec.ThrowAsException();
  } catch (CoreException const &e) {
    EXPECT_EQ(e.Error(), ec);
  }
}

TEST(ExecErrorDomain, MessageIsStableAndThrowsExecException) {
  ara::exec::ExecErrorDomain const domain(0x8000000000000202);
  char const *m1 = domain.Message(static_cast<int>(ara::exec::ExecErrc::kFailed));
  char const *m2 = domain.Message(static_cast<int>(ara::exec::ExecErrc::kFailed));
  EXPECT_STREQ(m1, m2);
  EXPECT_NE(std::string(m1).find("failed"), std::string::npos);
  EXPECT_STREQ(domain.Message(9999), "Unknown execution management error");
  ErrorCode const ec(static_cast<int>(ara::exec::ExecErrc::kFailed), domain);
  EXPECT_THROW(domain.ThrowAsException(ec), ara::exec::ExecException);
}

// ---- InstanceSpecifier -----------------------------------------------------
TEST(InstanceSpecifier, AcceptsValidIdentifiers) {
  for (char const *id : {"Port", "App/Port", "A1/b_2/C", "a"}) {
    auto r = InstanceSpecifier::Create(id);
    ASSERT_TRUE(r.HasValue()) << id;
    EXPECT_EQ(r.Value().ToString(), id);
  }
}

TEST(InstanceSpecifier, RejectsInvalidShortnames) {
  for (char const *id : {"", "1abc", "a-b", "a b", "App/1x", "App/a.b"}) {
    auto r = InstanceSpecifier::Create(id);
    ASSERT_FALSE(r.HasValue()) << id;
    EXPECT_TRUE(r.CheckError(CoreErrc::kInvalidMetaModelShortname)) << id;
  }
}

TEST(InstanceSpecifier, RejectsInvalidPaths) {
  for (char const *id : {"/App", "App/", "App//Port", "/"}) {
    auto r = InstanceSpecifier::Create(id);
    ASSERT_FALSE(r.HasValue()) << id;
    EXPECT_TRUE(r.CheckError(CoreErrc::kInvalidMetaModelPath)) << id;
  }
}

TEST(InstanceSpecifier, ConstructorThrowsOnInvalid) {
  EXPECT_THROW(InstanceSpecifier("bad id"), CoreException);
  EXPECT_NO_THROW(InstanceSpecifier("good/id"));
}

TEST(InstanceSpecifier, ComparisonAndHash) {
  InstanceSpecifier a("App/Port"), b("App/Port"), c("App/Other");
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_TRUE(c < a);
  EXPECT_TRUE(a == StringView("App/Port"));
  std::unordered_set<InstanceSpecifier> set{a, b, c};
  EXPECT_EQ(set.size(), 2U);
  std::set<InstanceSpecifier> ordered{a, c};
  EXPECT_EQ(ordered.begin()->ToString(), "App/Other");
}

// ---- basic type mappings ---------------------------------------------------
TEST(CoreTypes, AliasesBehaveLikeStdTypes) {
  Optional<int> o;
  EXPECT_FALSE(o.has_value());
  o = 3;
  EXPECT_EQ(*o, 3);
  Variant<int, String> v = String("x");
  EXPECT_TRUE(holds_alternative<String>(v));
  Vector<Byte> bytes{Byte{1}, Byte{2}};
  EXPECT_EQ(bytes.size(), 2U);
  Array<int, 3> arr{1, 2, 3};
  EXPECT_EQ(arr[2], 3);
  Map<String, int> m{{"a", 1}};
  EXPECT_EQ(m["a"], 1);
}

TEST(SteadyClock, IsMonotonic) {
  auto t1 = SteadyClock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  auto t2 = SteadyClock::now();
  EXPECT_GT(t2, t1);
  EXPECT_TRUE(SteadyClock::is_steady);
}

// ---- Initialize / Deinitialize --------------------------------------------
namespace {
int gInit = 0, gDeinit = 0;
Result<void> HookInit() { ++gInit; return {}; }
Result<void> HookDeinit() { ++gDeinit; return {}; }
Result<void> FailingInit() { return Result<void>(MakeErrorCode(CoreErrc::kInvalidArgument)); }
} // namespace

TEST(Initialization, LifecycleHooksRunInOrderAndRollBack) {
  ASSERT_FALSE(IsInitialized());
  EXPECT_TRUE(detail::RegisterLifecycleHook({"test.ok", &HookInit, &HookDeinit}));
  EXPECT_FALSE(detail::RegisterLifecycleHook({"test.ok", &HookInit, &HookDeinit})); // duplicate

  // A failing hook registered after "test.ok" must roll "test.ok" back.
  EXPECT_TRUE(detail::RegisterLifecycleHook({"test.fail", &FailingInit, nullptr}));
  auto failed = Initialize();
  ASSERT_FALSE(failed.HasValue());
  EXPECT_TRUE(failed.CheckError(CoreErrc::kInitializationFailed));
  EXPECT_EQ(gInit, 1);
  EXPECT_EQ(gDeinit, 1);
  EXPECT_FALSE(IsInitialized());
}

TEST(Initialization, DeinitializeWithoutInitializeFails) {
  EXPECT_TRUE(Deinitialize().CheckError(CoreErrc::kNotInitialized));
}
