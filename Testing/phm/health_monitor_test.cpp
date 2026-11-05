#include <thread>

#include "ara/phm/recovery_action.h"
#include "ara/phm/supervised_entity.h"
#include "phm_test_util.h"

using namespace phmtest;
using GS = GlobalSupervisionStatus;
using ES = ElementarySupervisionStatus;

namespace {
GlobalSupervisionConfig Global(char const *name, std::vector<std::string> members,
                               char const *recovery = "") {
  GlobalSupervisionConfig g;
  g.name = name;
  g.members = std::move(members);
  g.recoveryAction = recovery;
  g.executionError = 7;
  return g;
}

/// Feeds a healthy alive pattern (2 indications per 100 ms cycle).
void Healthy(HealthMonitor &m, int cycle) {
  m.ReportCheckpoint("E", 1, T(cycle * 100 + 10));
  m.ReportCheckpoint("E", 1, T(cycle * 100 + 60));
  m.Evaluate(T((cycle + 1) * 100));
}

class HealthMonitorTest : public ::testing::Test {
protected:
  void SetUp() override {
    wd = std::make_shared<FakeWatchdog>();
    mon = std::make_unique<HealthMonitor>(wd);
    ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
    ASSERT_TRUE(mon->AddGlobalSupervision(Global("G", {"Alive"}, "App/Recovery")).HasValue());
  }
  std::shared_ptr<FakeWatchdog> wd;
  std::unique_ptr<HealthMonitor> mon;
};
} // namespace

TEST_F(HealthMonitorTest, HealthySystemKicksWatchdogAndStaysOk) {
  mon->Start(T(0));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
  for (int c = 0; c < 5; ++c) Healthy(*mon, c);
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
  EXPECT_EQ(wd->alive, 5);
  EXPECT_EQ(wd->fired, 0);
}

TEST_F(HealthMonitorTest, ExpiryCallsRecoveryHandlerOnceWithDetails) {
  std::vector<ExpiredSupervision> events;
  RecoveryAction action(ara::core::InstanceSpecifier("App/Recovery"),
                        [&](ExpiredSupervision const &e) {
                          events.push_back(e);
                          ara::core::Promise<void> p;
                          p.set_value();
                          return p.get_future();
                        },
                        *mon);
  ASSERT_TRUE(action.Offer().HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100)); // no indication => EXPIRED (tolerance 0)
  mon->Evaluate(T(200));
  mon->Evaluate(T(300));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].globalSupervision, "G");
  EXPECT_EQ(events[0].supervision, "Alive");
  EXPECT_EQ(events[0].type, TypeOfSupervision::kAliveSupervision);
  EXPECT_EQ(events[0].executionError, 7U);
  EXPECT_EQ(wd->fired, 0); // recovery succeeded -> no watchdog reaction
}

TEST_F(HealthMonitorTest, FailedRecoveryEscalatesToWatchdog) {
  RecoveryAction action(ara::core::InstanceSpecifier("App/Recovery"),
                        [](ExpiredSupervision const &) {
                          ara::core::Promise<void> p;
                          p.SetError(MakeErrorCode(PhmErrc::kSMCanNotHandleRecovery));
                          return p.get_future();
                        },
                        *mon);
  ASSERT_TRUE(action.Offer().HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100)); // expiry, handler invoked, failure observed on next evaluation
  EXPECT_EQ(wd->fired, 0);
  mon->Evaluate(T(200));
  EXPECT_EQ(wd->fired, 1);
  EXPECT_TRUE(mon->WatchdogReactionFired());
  mon->Evaluate(T(300));
  EXPECT_EQ(wd->fired, 1); // only once
  int const aliveAfter = wd->alive;
  mon->Evaluate(T(400));
  EXPECT_EQ(wd->alive, aliveAfter); // no more alive notifications
}

TEST_F(HealthMonitorTest, NoRecoveryActionEscalatesImmediately) {
  mon->Start(T(0));
  mon->Evaluate(T(100));
  EXPECT_EQ(wd->fired, 1);
}

TEST_F(HealthMonitorTest, EscalationCanBeDisabled) {
  mon->Clear();
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  auto g = Global("G", {"Alive"}, "App/Recovery");
  g.escalateToWatchdog = false;
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);
  EXPECT_EQ(wd->fired, 0);
}

TEST_F(HealthMonitorTest, PlatformCriticalSupervisionStopsAfterTolerance) {
  mon->Clear();
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  auto g = Global("G", {"Alive"}, "App/Recovery");
  g.platformCritical = true;
  g.expiredTolerance = Ms(150);
  g.escalateToWatchdog = false; // STOPPED triggers the watchdog by itself
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());
  mon->Start(T(0));
  mon->Evaluate(T(100));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);
  mon->Evaluate(T(200)); // 100 ms since expiry < 150
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);
  EXPECT_EQ(wd->fired, 0);
  mon->Evaluate(T(260));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kSTOPPED);
  EXPECT_EQ(wd->fired, 1);
  mon->Evaluate(T(500));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kSTOPPED); // final
}

TEST_F(HealthMonitorTest, GlobalStatusIsWorstOfMembersAndListenerSeesTransitions) {
  mon->Clear();
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("A1", 2, 0, 0, /*tol*/ 2)).HasValue());
  AliveSupervision::Config c2;
  c2.checkpoint = CP("E2", 1);
  c2.expectedAliveIndications = 1;
  c2.referenceCycle = Ms(100);
  ASSERT_TRUE(mon->AddSupervision(std::make_shared<AliveSupervision>("A2", c2)).HasValue());
  ASSERT_TRUE(mon->AddGlobalSupervision(Global("G", {"A1", "A2"})).HasValue());
  std::vector<std::pair<GS, GS>> transitions;
  mon->SetStatusListener([&](std::string const &, GS from, GS to) { transitions.push_back({from, to}); });
  mon->Start(T(0));

  // cycle 0: A2 healthy, A1 misses -> FAILED (within tolerance)
  mon->ReportCheckpoint("E2", 1, T(50));
  mon->Evaluate(T(100));
  EXPECT_EQ(mon->GetElementaryStatus("A1").Value(), ES::kFAILED);
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kFAILED);
  // cycle 1: A1 healthy again, A2 healthy
  mon->ReportCheckpoint("E", 1, T(110));
  mon->ReportCheckpoint("E", 1, T(120));
  mon->ReportCheckpoint("E2", 1, T(150));
  mon->Evaluate(T(200));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
  // cycle 2: A2 misses -> EXPIRED (tolerance 0) dominates the healthy A1
  mon->ReportCheckpoint("E", 1, T(210));
  mon->ReportCheckpoint("E", 1, T(220));
  mon->Evaluate(T(300));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);

  ASSERT_EQ(transitions.size(), 3U);
  EXPECT_EQ(transitions[0], std::make_pair(GS::kOK, GS::kFAILED));
  EXPECT_EQ(transitions[1], std::make_pair(GS::kFAILED, GS::kOK));
  EXPECT_EQ(transitions[2], std::make_pair(GS::kOK, GS::kEXPIRED));
}

TEST_F(HealthMonitorTest, FunctionGroupStatesActivateAndDeactivateSupervisions) {
  mon->Clear();
  ASSERT_TRUE(mon->AddSupervision(MakeAlive("Alive", 2)).HasValue());
  auto g = Global("G", {"Alive"});
  g.functionGroup = "MachineFG";
  g.activeStates = {"Running"};
  g.escalateToWatchdog = false;
  ASSERT_TRUE(mon->AddGlobalSupervision(g).HasValue());

  mon->Start(T(0));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kDEACTIVATED);
  mon->Evaluate(T(1000));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kDEACTIVATED); // nothing supervised yet

  mon->SetFunctionGroupState("MachineFG", "Running", T(1000));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
  mon->Evaluate(T(1100)); // started with a fresh reference cycle, no indications -> EXPIRED
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);

  mon->SetFunctionGroupState("OtherFG", "Off", T(1200)); // unrelated group: no effect
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kEXPIRED);
  mon->SetFunctionGroupState("MachineFG", "Off", T(1200));
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kDEACTIVATED);
  mon->SetFunctionGroupState("MachineFG", "Running", T(1300)); // re-entering clears EXPIRED
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
}

TEST_F(HealthMonitorTest, DisableIgnoresCheckpointsAndSuspendsEvaluationEnableRestarts) {
  mon->Start(T(0));
  Healthy(*mon, 0);
  mon->SetEntityEnabled("E", false, T(100));
  mon->Evaluate(T(500)); // would have been several missed cycles
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
  mon->ReportCheckpoint("E", 1, T(510)); // ignored
  mon->SetEntityEnabled("E", true, T(600));
  mon->ReportCheckpoint("E", 1, T(610));
  mon->ReportCheckpoint("E", 1, T(650));
  mon->Evaluate(T(700)); // first full cycle after Enable() is judged on its own
  EXPECT_EQ(mon->GetGlobalStatus("G").Value(), GS::kOK);
}

TEST_F(HealthMonitorTest, ConfigurationValidation) {
  EXPECT_TRUE(mon->AddSupervision(MakeAlive("Alive")).CheckError(PhmErrc::kInvalidConfiguration)); // duplicate
  EXPECT_TRUE(mon->AddGlobalSupervision(Global("G2", {"missing"})).CheckError(PhmErrc::kUnknownSupervision));
  EXPECT_TRUE(mon->AddGlobalSupervision(Global("G", {"Alive"})).CheckError(PhmErrc::kInvalidConfiguration));
  EXPECT_TRUE(mon->AddGlobalSupervision(Global("G3", {})).CheckError(PhmErrc::kInvalidConfiguration));
  EXPECT_TRUE(mon->GetGlobalStatus("nope").CheckError(PhmErrc::kUnknownSupervision));
  EXPECT_TRUE(mon->GetElementaryStatus("nope").CheckError(PhmErrc::kUnknownSupervision));
}

TEST_F(HealthMonitorTest, RecoveryActionOfferRules) {
  auto handler = [](ExpiredSupervision const &) {
    ara::core::Promise<void> p;
    p.set_value();
    return p.get_future();
  };
  ara::phm::RecoveryAction a(ara::core::InstanceSpecifier("App/Recovery"), handler, *mon);
  ara::phm::RecoveryAction b(ara::core::InstanceSpecifier("App/Recovery"), handler, *mon);
  EXPECT_TRUE(a.Offer().HasValue());
  EXPECT_TRUE(b.Offer().CheckError(PhmErrc::kAlreadyOffered));
  a.StopOffer();
  EXPECT_TRUE(b.Offer().HasValue());
}

// ---------------------------------------------------------------------------
// SupervisedEntity API
// ---------------------------------------------------------------------------
TEST(SupervisedEntity, ReportsThroughTheConfiguredSinkAndHonoursEnableDisable) {
  struct Recorder : CheckpointSink {
    std::vector<std::pair<std::string, CheckpointType>> seen;
    std::vector<bool> enabled;
    bool IsKnownEntity(std::string const &e) const override { return e == "App/Known"; }
    void ReportCheckpoint(std::string const &e, CheckpointType c) override { seen.push_back({e, c}); }
    void SetEntityEnabled(std::string const &, bool en) override { enabled.push_back(en); }
  };
  auto rec = std::make_shared<Recorder>();
  SetCheckpointSink(rec);
  auto entity = SupervisedEntity::Create(ara::core::InstanceSpecifier("App/Known"));
  ASSERT_TRUE(entity.HasValue());
  entity.Value().ReportCheckpoint(3);
  entity.Value().Disable();
  entity.Value().Enable();
  EXPECT_EQ(rec->seen, (std::vector<std::pair<std::string, CheckpointType>>{{"App/Known", 3}}));
  EXPECT_EQ(rec->enabled, (std::vector<bool>{false, true}));

  EXPECT_TRUE(SupervisedEntity::Create(ara::core::InstanceSpecifier("App/Unknown")).CheckError(PhmErrc::kUnknownEntity));
  EXPECT_THROW(SupervisedEntity(ara::core::InstanceSpecifier("App/Unknown")), PhmException);
  EXPECT_NO_THROW(SupervisedEntity(ara::core::InstanceSpecifier("App/Known")));

  auto moved = std::move(entity).Value(); // movable
  moved.ReportCheckpoint(4);
  EXPECT_EQ(rec->seen.size(), 2U);
  SetCheckpointSink(nullptr);
}

TEST(SupervisedEntity, EndToEndWithTheProcessWideMonitorAndBackgroundThread) {
  auto &mon = HealthMonitor::Instance();
  mon.Clear();
  auto wd = std::make_shared<FakeWatchdog>();
  mon.SetWatchdog(wd);
  ASSERT_TRUE(mon.AddSupervision(MakeAlive("Alive", 1, 0, 1, 0, /*cycleMs*/ 50)).HasValue());
  ASSERT_TRUE(mon.AddGlobalSupervision(Global("G", {"Alive"})).HasValue());
  mon.Start(ara::core::SteadyClock::now());
  mon.StartBackgroundEvaluation(std::chrono::milliseconds(10));

  auto entity = SupervisedEntity::Create(ara::core::InstanceSpecifier("E"));
  ASSERT_TRUE(entity.HasValue());
  for (int i = 0; i < 12; ++i) { // ~25 ms period => 1-2 indications per 50 ms cycle
    entity.Value().ReportCheckpoint(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  EXPECT_EQ(mon.GetGlobalStatus("G").Value(), GS::kOK);
  EXPECT_GT(wd->alive.load(), 3);

  std::this_thread::sleep_for(std::chrono::milliseconds(200)); // entity stops reporting
  EXPECT_EQ(mon.GetGlobalStatus("G").Value(), GS::kEXPIRED);
  EXPECT_EQ(wd->fired.load(), 1);
  mon.Clear();
}

// ---------------------------------------------------------------------------
// configuration file
// ---------------------------------------------------------------------------
TEST(PhmConfig, ParsesAllSupervisionTypes) {
  auto cfg = PhmConfig::FromJson(R"({
    "supervisions": [
      {"name":"A","type":"alive","entity":"E","checkpoint":1,"expectedAliveIndications":3,
       "minMargin":1,"maxMargin":1,"referenceCycleMs":100,"failedCyclesTolerance":2},
      {"name":"D","type":"deadline","entity":"E","startCheckpoint":1,"endCheckpoint":2,"minMs":5,"maxMs":50},
      {"name":"L","type":"logical","entity":"E","checkpoints":[1,2,3],"initial":[1],"final":[3],
       "transitions":[[1,2],[2,3]]}],
    "globalSupervisions": [
      {"name":"G","members":["A","D","L"],"functionGroup":"FG","activeStates":["Running"],
       "recoveryAction":"App/R","expiredToleranceMs":20,"platformCritical":true,"executionError":9}]
  })");
  ASSERT_TRUE(cfg.HasValue()) << cfg.Error().Message();
  EXPECT_EQ(cfg.Value().supervisions.size(), 3U);
  EXPECT_EQ(cfg.Value().supervisions[0]->Type(), TypeOfSupervision::kAliveSupervision);
  EXPECT_EQ(cfg.Value().supervisions[1]->Type(), TypeOfSupervision::kDeadlineSupervision);
  EXPECT_EQ(cfg.Value().supervisions[2]->Type(), TypeOfSupervision::kLogicalSupervision);
  ASSERT_EQ(cfg.Value().globals.size(), 1U);
  EXPECT_TRUE(cfg.Value().globals[0].platformCritical);
  EXPECT_EQ(cfg.Value().globals[0].expiredTolerance, Ms(20));

  HealthMonitor mon;
  EXPECT_TRUE(mon.Configure(std::move(cfg).Value()).HasValue());
  EXPECT_TRUE(mon.IsKnownEntity("E"));
  EXPECT_FALSE(mon.IsKnownEntity("X"));
}

TEST(PhmConfig, RejectsBrokenDocuments) {
  for (char const *bad : {
           "nonsense",
           "[]",
           R"({"supervisions":[{"name":"A","type":"alive","entity":"E"}]})",                    // no checkpoint
           R"({"supervisions":[{"name":"A","type":"magic","entity":"E"}]})",                    // unknown type
           R"({"supervisions":[{"name":"A","type":"alive","entity":"E","checkpoint":1,"referenceCycleMs":0}]})",
           R"({"supervisions":[{"name":"D","type":"deadline","entity":"E","startCheckpoint":1,"endCheckpoint":2,"minMs":9,"maxMs":1}]})",
           R"({"supervisions":[{"name":"L","type":"logical","entity":"E","checkpoints":[1],"initial":[2],"final":[1],"transitions":[]}]})",
           R"({"globalSupervisions":[{"name":"G"}]})"}) {
    EXPECT_TRUE(PhmConfig::FromJson(bad).CheckError(PhmErrc::kInvalidConfiguration)) << bad;
  }
  EXPECT_FALSE(PhmConfig::FromFile("/no/such/file.json").HasValue());
}

TEST(PhmErrorDomain, MessageAndThrow) {
  auto ec = MakeErrorCode(PhmErrc::kSMCanNotHandleRecovery);
  EXPECT_STREQ(ec.Domain().Name(), "Phm");
  EXPECT_THROW(ec.ThrowAsException(), PhmException);
}
