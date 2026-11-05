// Tests that pin behaviour required by the AUTOSAR AP R25-11 SWS PlatformHealthManagement
// (requirement ids as quoted in doc/spec-conformance/phm.md).
#include "ara/phm/recovery_action.h"
#include "phm_test_util.h"

using namespace phmtest;
using GS = GlobalSupervisionStatus;
using ES = ElementarySupervisionStatus;

namespace {
struct SpecFixture : ::testing::Test {
  void SetUp() override {
    wd = std::make_shared<FakeWatchdog>();
    mon = std::make_unique<HealthMonitor>(wd);
  }
  std::shared_ptr<FakeWatchdog> wd;
  std::unique_ptr<HealthMonitor> mon;
};

ara::core::Future<void> Ok() {
  ara::core::Promise<void> p;
  p.set_value();
  return p.get_future();
}
} // namespace

// SWS_PHM_01361: without a configured execution error PHM reports the value 1.
TEST_F(SpecFixture, DefaultExecutionErrorIsOne) {
  GlobalSupervisionConfig g;
  g.name = "G";
  g.members = {"Alive"};
  g.recoveryAction = "App/R";
  EXPECT_EQ(g.executionError, 1U);
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  std::vector<ExpiredSupervision> events;
  RecoveryAction ra(ara::core::InstanceSpecifier("App/R"),
                    [&](ExpiredSupervision const &e) {
                      events.push_back(e);
                      return Ok();
                    },
                    *mon);
  ASSERT_TRUE(ra.Offer().HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100));
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].executionError, 1U);
}

TEST(SpecConfig, MissingExecutionErrorParsesAsOne) {
  auto cfg = PhmConfig::FromJson(
      R"({"supervisions":[{"name":"A","type":"alive","entity":"E","checkpoint":1,"expectedAliveIndications":1,"referenceCycleMs":100}],
          "globalSupervisions":[{"name":"G","members":["A"]}]})");
  ASSERT_TRUE(cfg.HasValue());
  EXPECT_EQ(cfg.Value().globals.at(0).executionError, 1U);
}

// SWS_PHM_00218 / SWS_PHM_01344: every status starts as kDEACTIVATED.
TEST_F(SpecFixture, StatusesStartDeactivatedUntilActivated) {
  auto a = MakeAlive("Alive", 2);
  EXPECT_EQ(a->Status(), ES::kDEACTIVATED);
  ASSERT_TRUE(mon->AddSupervision(a).HasValue());
  GlobalSupervisionConfig g;
  g.name = "G";
  g.members = {"Alive"};
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kDEACTIVATED);
  EXPECT_EQ(mon->GetElementaryStatus("Alive").Value(), ES::kDEACTIVATED);
}

// SWS_PHM_01346/01347: alive supervision goes kFAILED within the tolerance, kEXPIRED beyond it;
// deadline and logical supervision go straight to kEXPIRED.
TEST(SpecSupervision, FailedThenExpiredForAliveDirectExpiredForOthers) {
  auto alive = MakeAlive("A", 2, 0, 0, /*tolerance*/ 1);
  alive->Activate(T(0));
  alive->Evaluate(T(100));
  EXPECT_EQ(alive->Status(), ES::kFAILED);
  alive->Evaluate(T(200));
  EXPECT_EQ(alive->Status(), ES::kEXPIRED);

  DeadlineSupervision::Config dc;
  dc.start = CP("E", 1);
  dc.end = CP("E", 2);
  dc.maxDeadline = Ms(10);
  DeadlineSupervision d("D", dc);
  d.Activate(T(0));
  d.OnCheckpoint(CP("E", 1), T(0));
  d.Evaluate(T(50));
  EXPECT_EQ(d.Status(), ES::kEXPIRED);

  LogicalSupervision::Config lc;
  lc.entity = "E";
  lc.checkpoints = {1, 2};
  lc.initial = {1};
  lc.final = {2};
  lc.transitions = {{1, 2}};
  LogicalSupervision l("L", lc);
  l.Activate(T(0));
  l.OnCheckpoint(CP("E", 2), T(1)); // first report is not an initial checkpoint
  EXPECT_EQ(l.Status(), ES::kEXPIRED);
}

// SWS_PHM_00107: if the recovery cannot be handled PHM stops the alive notifications and
// fires the watchdog reaction (once).
TEST_F(SpecFixture, UnhandledRecoveryStopsAliveNotificationsAndFiresWatchdog) {
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  GlobalSupervisionConfig g;
  g.name = "G";
  g.members = {"Alive"};
  g.recoveryAction = "App/R";
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  RecoveryAction ra(ara::core::InstanceSpecifier("App/R"),
                    [](ExpiredSupervision const &) {
                      ara::core::Promise<void> p;
                      p.SetError(MakeErrorCode(PhmErrc::kSMCanNotHandleRecovery));
                      return p.get_future();
                    },
                    *mon);
  ASSERT_TRUE(ra.Offer().HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100));
  mon->Evaluate(T(200));
  EXPECT_EQ(wd->fired, 1);
  int const alive = wd->alive;
  mon->Evaluate(T(300));
  mon->Evaluate(T(400));
  EXPECT_EQ(wd->alive, alive); // no further kicks
  EXPECT_EQ(wd->fired, 1);
}

// SWS_PHM_01364/01365/01366: Disable() ignores checkpoints, Enable() restarts the alive cycle.
TEST_F(SpecFixture, DisabledEntityIgnoresCheckpointsAndEnableRestartsCycle) {
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  GlobalSupervisionConfig g;
  g.name = "G";
  g.members = {"Alive"};
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  mon->Start(T(0));
  mon->SetEntityEnabled("E", false, T(10));
  mon->ReportCheckpoint("E", 1, T(20)); // ignored
  mon->Evaluate(T(500));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK) << "no evaluation while disabled";
  mon->SetEntityEnabled("E", true, T(500));
  mon->ReportCheckpoint("E", 1, T(510));
  mon->ReportCheckpoint("E", 1, T(560));
  mon->Evaluate(T(600)); // first cycle after Enable() has exactly the expected indications
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
}

// SWS_PHM_00216: the five global statuses exist; kSTOPPED only via platform-critical supervision.
TEST_F(SpecFixture, StoppedOnlyForPlatformCriticalSupervisions) {
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  GlobalSupervisionConfig g;
  g.name = "G";
  g.members = {"Alive"};
  g.escalateToWatchdog = false;
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100));
  mon->Evaluate(T(200));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED); // not kSTOPPED
}
