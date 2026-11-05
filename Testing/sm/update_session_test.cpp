// Tests for the State Management UpdateRequest rules (update_session.h) and a
// compile/link check of the SOME/IP skeleton that wraps them.
#include "ara/sm/update_request/update_session.h"
#include "ara/sm/update_request/updaterequest_skeleton.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

using ara::sm::errorDomains;
using ara::sm::update_request::UpdateSession;
using Phase = UpdateSession::Phase;

TEST(SmErrors, OperationRejectedKeepsLegacyAlias) {
  EXPECT_EQ(errorDomains::kOperationRejected, errorDomains::kRejected);
  EXPECT_NE(errorDomains::kOperationRejected, errorDomains::kNotAllowedMultipleUpdateSessions);
}

TEST(SmSession, StartsInactive) {
  UpdateSession s;
  EXPECT_FALSE(s.Active());
  EXPECT_EQ(s.GetPhase(), Phase::kInactive);
}

TEST(SmSession, OnlyOneSessionAtATime) { // 00209
  UpdateSession s;
  EXPECT_FALSE(s.RequestUpdateSession().has_value());
  EXPECT_TRUE(s.Active());
  auto e = s.RequestUpdateSession();
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(*e, errorDomains::kNotAllowedMultipleUpdateSessions);
}

TEST(SmSession, OperationsOutsideASessionAreRejected) { // 00213
  UpdateSession s;
  for (auto const &r : {s.PrepareUpdate(), s.VerifyUpdate(), s.PrepareRollback(),
                        s.StopUpdateSession(), s.ResetMachine()}) {
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, errorDomains::kOperationRejected);
  }
  EXPECT_FALSE(s.Active());
}

TEST(SmSession, UpdateFlowPrepareVerifyStop) {
  UpdateSession s;
  ASSERT_FALSE(s.RequestUpdateSession());
  EXPECT_FALSE(s.PrepareUpdate());
  EXPECT_EQ(s.GetPhase(), Phase::kPrepared);
  EXPECT_FALSE(s.VerifyUpdate());
  EXPECT_EQ(s.GetPhase(), Phase::kVerified);
  EXPECT_FALSE(s.StopUpdateSession());
  EXPECT_FALSE(s.Active());
}

TEST(SmSession, RollbackFlow) {
  UpdateSession s;
  ASSERT_FALSE(s.RequestUpdateSession());
  ASSERT_FALSE(s.PrepareUpdate());
  EXPECT_FALSE(s.PrepareRollback());
  EXPECT_EQ(s.GetPhase(), Phase::kRollbackPrepared);
  EXPECT_FALSE(s.StopUpdateSession());
}

TEST(SmSession, SessionCanBeRestartedAfterStop) {
  UpdateSession s;
  ASSERT_FALSE(s.RequestUpdateSession());
  ASSERT_FALSE(s.StopUpdateSession());
  EXPECT_FALSE(s.RequestUpdateSession());
}

TEST(SmSession, ResetMachineIsAcceptedInsideASessionAndClearedOnStop) {
  UpdateSession s;
  ASSERT_FALSE(s.RequestUpdateSession());
  EXPECT_FALSE(s.ResetRequested());
  EXPECT_FALSE(s.ResetMachine());
  EXPECT_TRUE(s.ResetRequested());
  s.StopUpdateSession();
  EXPECT_FALSE(s.ResetRequested());
}

TEST(SmSession, StatusSurvivesARestart) { // 00204
  std::string path = ::testing::TempDir() + "sm_session.txt";
  {
    UpdateSession a;
    ASSERT_FALSE(a.RequestUpdateSession());
    ASSERT_FALSE(a.PrepareUpdate());
    ASSERT_TRUE(a.Save(path));
  }
  UpdateSession b;
  ASSERT_TRUE(b.Load(path));
  EXPECT_TRUE(b.Active());
  EXPECT_EQ(b.GetPhase(), Phase::kPrepared);
  // the restored session blocks a second one
  EXPECT_EQ(*b.RequestUpdateSession(), errorDomains::kNotAllowedMultipleUpdateSessions);
  std::remove(path.c_str());
}

TEST(SmSession, LoadRejectsMissingOrCorruptFiles) {
  UpdateSession s;
  EXPECT_FALSE(s.Load(::testing::TempDir() + "does_not_exist"));
  std::string path = ::testing::TempDir() + "sm_session_bad.txt";
  { std::FILE *f = std::fopen(path.c_str(), "w"); std::fputs("99\n", f); std::fclose(f); }
  EXPECT_FALSE(s.Load(path));
  { std::FILE *f = std::fopen(path.c_str(), "w"); std::fputs("junk\n", f); std::fclose(f); }
  EXPECT_FALSE(s.Load(path));
  EXPECT_FALSE(s.Active());
  std::remove(path.c_str());
}

TEST(SmSession, ExactlyOneConcurrentRequestWins) {
  UpdateSession s;
  std::atomic<int> won{0};
  std::vector<std::thread> ts;
  for (int i = 0; i < 16; ++i)
    ts.emplace_back([&] { if (!s.RequestUpdateSession()) ++won; });
  for (auto &t : ts) t.join();
  EXPECT_EQ(won.load(), 1);
}

TEST(SmSkeleton, UpdateRequestSkeletonCompilesAndIsConcrete) {
  using Sk = ara::sm::update_request::skeleton::update_request_Skeleton;
  static_assert(!std::is_abstract_v<Sk>, "skeleton must be instantiable");
  static_assert(std::is_same_v<decltype(std::declval<Sk &>().Session()), UpdateSession &>);
  SUCCEED();
}
