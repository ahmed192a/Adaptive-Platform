// Tests for the configurable State Management model (state_machine.h).
#include "ara/sm/state_machine/state_machine.h"

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <thread>

using namespace ara::sm::state_machine;

namespace {

std::unique_ptr<StateMachine> MakeMachine() {
  auto smp = std::make_unique<StateMachine>("Machine", std::vector<std::string>{"Initial", "Off", "Driving", "Parking", "PrepareUpdate", "VerifyUpdate",
                              "PrepareRollback"},
                  "Initial");
  StateMachine &sm = *smp;
  sm.AddTransition("Initial", "Driving");
  sm.AddTransition("Driving", "Parking");
  sm.AddTransition("Parking", "Driving");
  sm.AddTransition("*", "Off");
  return smp;
}

} // namespace

TEST(SmModel, MinimumStatesAreChecked) { // section 7.7.1
  EXPECT_TRUE(MakeMachine()->HasMinimumStates());
  StateMachine small("X", {"A", "B"}, "A");
  EXPECT_FALSE(small.HasMinimumStates());
}

TEST(SmModel, StartsInTheInitialState) {
  auto smp = MakeMachine();
  auto &sm = *smp;
  EXPECT_EQ(sm.CurrentState(), "Initial");
}

TEST(SmModel, ValidTransitionChangesStateAndNotifies) { // 00600 / 00665
  auto smp = MakeMachine();
  auto &sm = *smp;
  std::string seen;
  sm.SetNotifier([&](std::string const &f, std::string const &t) { seen += f + ">" + t + ";"; });
  EXPECT_FALSE(sm.RequestTransition("Driving").has_value());
  EXPECT_EQ(sm.CurrentState(), "Driving");
  EXPECT_FALSE(sm.RequestTransition("Parking").has_value());
  EXPECT_EQ(seen, "Initial>Driving;Driving>Parking;");
}

TEST(SmModel, UnknownStateIsInvalidValue) { // 00604
  auto smp = MakeMachine();
  auto &sm = *smp;
  EXPECT_EQ(sm.RequestTransition("Flying"), TransitionResult(TransitionErrc::kInvalidValue));
  EXPECT_EQ(sm.CurrentState(), "Initial");
}

TEST(SmModel, MissingTransitionIsNotAllowed) { // 00603
  auto smp = MakeMachine();
  auto &sm = *smp;
  EXPECT_EQ(sm.RequestTransition("Parking"), TransitionResult(TransitionErrc::kTransitionNotAllowed));
  EXPECT_EQ(sm.CurrentState(), "Initial");
}

TEST(SmModel, WildcardTransitionWorksFromAnyState) {
  auto smp = MakeMachine();
  auto &sm = *smp;
  sm.RequestTransition("Driving");
  EXPECT_FALSE(sm.RequestTransition("Off").has_value());
  EXPECT_EQ(sm.CurrentState(), "Off");
}

TEST(SmModel, ActionsRunInOrder) {
  StateMachine sm("M", {"A", "B"}, "A");
  std::string order;
  sm.AddTransition("A", "B", {{"one", [&] { order += "1"; return true; }}, {"two", [&] { order += "2"; return true; }}});
  EXPECT_FALSE(sm.RequestTransition("B").has_value());
  EXPECT_EQ(order, "12");
}

TEST(SmModel, FailingActionIsTransitionFailedAndKeepsTheStateAndStopsTheList) { // 00650
  StateMachine sm("M", {"A", "B"}, "A");
  bool second = false;
  sm.AddTransition("A", "B", {{"bad", [] { return false; }}, {"never", [&] { second = true; return true; }}});
  EXPECT_EQ(sm.RequestTransition("B"), TransitionResult(TransitionErrc::kTransitionFailed));
  EXPECT_EQ(sm.CurrentState(), "A");
  EXPECT_FALSE(second);
}

TEST(SmModel, RecoveryTransitionBlocksNormalRequests) { // 00605
  auto smp = MakeMachine();
  auto &sm = *smp;
  sm.BeginRecovery();
  EXPECT_EQ(sm.RequestTransition("Driving"), TransitionResult(TransitionErrc::kRecoveryTransitionOngoing));
  sm.EndRecovery();
  EXPECT_FALSE(sm.RequestTransition("Driving").has_value());
}

TEST(SmModel, NotOfferedRefusesRequests) { // 00618 / 00619
  auto smp = MakeMachine();
  auto &sm = *smp;
  sm.SetOffered(false);
  EXPECT_TRUE(sm.RequestTransition("Driving").has_value());
  sm.SetOffered(true);
  EXPECT_FALSE(sm.RequestTransition("Driving").has_value());
}

TEST(SmModel, NewValidRequestCancelsTheRunningOne) { // 00607 / 00606
  StateMachine sm("M", {"A", "B", "C"}, "A");
  std::promise<void> started, release;
  auto releaseF = release.get_future().share();
  sm.AddTransition("A", "B", {{"slow", [&] { started.set_value(); releaseF.wait(); return true; }},
                              {"after", [] { return true; }}});
  sm.AddTransition("A", "C");
  auto first = std::async(std::launch::async, [&] { return sm.RequestTransition("B"); });
  started.get_future().wait();
  EXPECT_FALSE(sm.RequestTransition("C").has_value()); // newer request wins
  release.set_value();
  EXPECT_EQ(first.get(), TransitionResult(TransitionErrc::kOperationCanceled));
  EXPECT_EQ(sm.CurrentState(), "C");
}

TEST(SmModel, InvalidNewRequestDoesNotCancelTheRunningOne) {
  StateMachine sm("M", {"A", "B"}, "A");
  std::promise<void> started, release;
  auto releaseF = release.get_future().share();
  sm.AddTransition("A", "B", {{"slow", [&] { started.set_value(); releaseF.wait(); return true; }}});
  auto first = std::async(std::launch::async, [&] { return sm.RequestTransition("B"); });
  started.get_future().wait();
  EXPECT_EQ(sm.RequestTransition("Nope"), TransitionResult(TransitionErrc::kInvalidValue));
  release.set_value();
  EXPECT_FALSE(first.get().has_value());
  EXPECT_EQ(sm.CurrentState(), "B");
}

TEST(SmModel, ConcurrentRequestsEndInOneOfTheRequestedStates) {
  StateMachine sm("M", {"A", "B", "C"}, "A");
  sm.AddTransition("A", "B");
  sm.AddTransition("A", "C");
  std::vector<std::thread> ts;
  for (int i = 0; i < 8; ++i) ts.emplace_back([&, i] { sm.RequestTransition(i % 2 ? "B" : "C"); });
  for (auto &t : ts) t.join();
  EXPECT_TRUE(sm.CurrentState() == "B" || sm.CurrentState() == "C");
}

// ------------------------------------------------------------- StateManager
TEST(SmManager, InitialTransitionResultIsUnknownUntilStart) {
  StateManager mgr;
  EXPECT_FALSE(mgr.GetInitialMachineStateTransitionResult().has_value());
}

TEST(SmManager, StartRecordsTheOutcome) {
  StateManager mgr;
  auto &m = mgr.Add("Machine", {"Initial", "Driving"}, "Initial");
  m.AddTransition("Initial", "Driving");
  EXPECT_FALSE(mgr.Start("Machine", "Driving").has_value());
  auto r = mgr.GetInitialMachineStateTransitionResult();
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(r->has_value());
  EXPECT_EQ(m.CurrentState(), "Driving");
}

TEST(SmManager, FailedStartIsReported) {
  StateManager mgr;
  auto &m = mgr.Add("Machine", {"Initial", "Driving"}, "Initial");
  m.AddTransition("Initial", "Driving", {{"x", [] { return false; }}});
  EXPECT_TRUE(mgr.Start("Machine", "Driving").has_value());
  EXPECT_EQ(*mgr.GetInitialMachineStateTransitionResult(), TransitionResult(TransitionErrc::kTransitionFailed));
}

TEST(SmManager, StartOfAnUnknownMachineIsInvalidValue) {
  StateManager mgr;
  EXPECT_EQ(mgr.Start("Nope", "X"), TransitionResult(TransitionErrc::kInvalidValue));
  EXPECT_EQ(mgr.Find("Nope"), nullptr);
}

// --------------------------------------------------------------- recovery
TEST(SmRecovery, KnownFunctionGroupIsRecovered) { // 00030
  RecoveryHandler rh;
  std::string got;
  rh.Register("FG_Brake", [&](std::string const &fg) { got = fg; return true; });
  EXPECT_FALSE(rh.Handle("FG_Brake").has_value());
  EXPECT_EQ(got, "FG_Brake");
}

TEST(SmRecovery, UnknownFunctionGroupCannotBeHandled) { // 00031
  RecoveryHandler rh;
  EXPECT_EQ(rh.Handle("FG_Unknown"), RecoveryResult(RecoveryErrc::kSMCanNotHandleRecovery));
}

TEST(SmRecovery, FailingHandlerCannotHandle) {
  RecoveryHandler rh;
  rh.Register("FG", [](std::string const &) { return false; });
  EXPECT_EQ(rh.Handle("FG"), RecoveryResult(RecoveryErrc::kSMCanNotHandleRecovery));
}

// ----------------------------------------------------------- network handle
TEST(SmNetwork, NetworkStateTriggersTheConfiguredTransition) { // 00620 / 00621
  auto smp = MakeMachine();
  auto &sm = *smp;
  NetworkHandleBridge nb(sm);
  nb.Map("CAN0", NetworkHandleState::kFullCom, "Driving");
  nb.Map("CAN0", NetworkHandleState::kNoCom, "Off");
  EXPECT_FALSE(nb.OnNetworkState("CAN0", NetworkHandleState::kFullCom).has_value());
  EXPECT_EQ(sm.CurrentState(), "Driving");
  EXPECT_EQ(nb.Current("CAN0"), NetworkHandleState::kFullCom);
  EXPECT_FALSE(nb.OnNetworkState("CAN0", NetworkHandleState::kNoCom).has_value());
  EXPECT_EQ(sm.CurrentState(), "Off");
}

TEST(SmNetwork, UnmappedChangeIsRecordedButChangesNothing) {
  auto smp = MakeMachine();
  auto &sm = *smp;
  NetworkHandleBridge nb(sm);
  EXPECT_FALSE(nb.OnNetworkState("ETH0", NetworkHandleState::kFullCom).has_value());
  EXPECT_EQ(sm.CurrentState(), "Initial");
  EXPECT_EQ(nb.Current("ETH0"), NetworkHandleState::kFullCom);
  EXPECT_FALSE(nb.Current("other").has_value());
}

TEST(SmNetwork, TransitionErrorsArePropagated) {
  auto smp = MakeMachine();
  auto &sm = *smp;
  NetworkHandleBridge nb(sm);
  nb.Map("CAN0", NetworkHandleState::kFullCom, "Parking"); // not allowed from Initial
  EXPECT_EQ(nb.OnNetworkState("CAN0", NetworkHandleState::kFullCom),
            TransitionResult(TransitionErrc::kTransitionNotAllowed));
}

TEST(SmNetwork, SetNetworkHandleReportsTheWantedStateToTheRequester) {
  auto smp = MakeMachine();
  auto &sm = *smp;
  NetworkHandleBridge nb(sm);
  std::string got;
  nb.OnRequest([&](std::string const &h, NetworkHandleState s) {
    got = h + (s == NetworkHandleState::kFullCom ? ":Full" : ":No");
  });
  nb.SetNetworkHandle("CAN0", NetworkHandleState::kFullCom);
  EXPECT_EQ(got, "CAN0:Full");
  EXPECT_EQ(nb.Wanted("CAN0"), NetworkHandleState::kFullCom);
}
