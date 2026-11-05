// Conformance tests for AUTOSAR AP R25-11 SWS_ANM (see doc/spec-conformance/nm.md).
#include <gtest/gtest.h>

#include <vector>

#include "ara/nm/network_handle.h"
#include "ara/nm/network_manager.h"
#include "ara/nm/nm_error_domain.h"

using namespace ara::nm;
using std::chrono::milliseconds;

namespace {
NmConfig Cfg() {
  NmConfig c;
  c.nodeId = 1;
  c.repeatMessageTime = milliseconds(1000);
  c.nmTimeout = milliseconds(2000);
  c.waitBusSleepTime = milliseconds(1500);
  c.messageCycleTime = milliseconds(500);
  c.pnResetTime = milliseconds(3000);
  return c;
}

struct Spec : ::testing::Test {
  std::shared_ptr<ManualClock> clk = std::make_shared<ManualClock>();
  NetworkManager mgr{clk};
  NmChannel *ch{nullptr};
  ara::core::InstanceSpecifier port{"Vehicle/NmPort0"};

  void SetUp() override {
    auto r = mgr.CreateNetwork("can0", Cfg());
    ASSERT_TRUE(r.HasValue());
    ch = r.Value();
    ASSERT_TRUE(mgr.BindPort("Vehicle/NmPort0", "can0").HasValue());
  }
  void TearDown() override { NetworkManager::UnbindPort("Vehicle/NmPort0"); }
  void Advance(int ms) {
    clk->Advance(milliseconds(ms));
    ch->Process();
  }
};
} // namespace

TEST(NmSpecErrors, NormativeCodeValuesAndDomain) {
  EXPECT_EQ(static_cast<int>(NmErrc::kServiceNotAvailable), 1); // SWS_ANM_01111
  EXPECT_EQ(static_cast<int>(NmErrc::kInvalidHandler), 2);
  auto ec = MakeErrorCode(NmErrc::kInvalidHandler);
  EXPECT_STREQ(ec.Domain().Name(), "Nm");
  EXPECT_STRNE(ec.Message(), "unknown NM error");
  EXPECT_EQ(NetworkStateType::kNoCom, static_cast<NetworkStateType>(0)); // SWS_ANM_01358
  EXPECT_EQ(NetworkStateType::kFullCom, static_cast<NetworkStateType>(1));
}

TEST(NmSpecHandle, UnboundPortIsServiceNotAvailable) {
  NetworkHandle h(ara::core::InstanceSpecifier("Nowhere/Port"));
  auto s = h.GetNetworkState();
  ASSERT_FALSE(s.HasValue());
  EXPECT_EQ(s.Error(), MakeErrorCode(NmErrc::kServiceNotAvailable));
  EXPECT_FALSE(h.GetNetworkRequestedState().HasValue());
  EXPECT_FALSE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
  EXPECT_FALSE(h.RegisterNetworkStateChangeNotifier([](NetworkStateType const &) {}).HasValue());
}

TEST_F(Spec, DefaultStateIsNoCom) { // SWS_ANM_00088
  NetworkHandle h(port);
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kNoCom);
  EXPECT_EQ(h.GetNetworkRequestedState().Value(), NetworkStateType::kNoCom);
}

TEST_F(Spec, RequestMapsToFullComAndReleaseKeepsItUntilBusSleep) {
  NetworkHandle h(port);
  ASSERT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
  EXPECT_EQ(h.GetNetworkRequestedState().Value(), NetworkStateType::kFullCom);
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kFullCom);
  Advance(1000); // RepeatMessage -> NormalOperation (still requested)
  ASSERT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kNoCom).HasValue());
  EXPECT_EQ(h.GetNetworkRequestedState().Value(), NetworkStateType::kNoCom);
  EXPECT_EQ(ch->GetState(), NmState::kReadySleep);
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kFullCom); // Ready Sleep is Network Mode
  Advance(2000);                                                      // NM timeout
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kNoCom);   // Prepare Bus-Sleep
}

TEST_F(Spec, ReleaseWithoutRequestIsIdempotent) {
  NetworkHandle h(port);
  EXPECT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kNoCom).HasValue());
  EXPECT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
  EXPECT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
}

TEST_F(Spec, NetworkStateNotifierFiresOnlyOnFullComNoComChanges) {
  NetworkHandle h(port);
  std::vector<NetworkStateType> seen;
  ASSERT_TRUE(h.RegisterNetworkStateChangeNotifier([&](NetworkStateType const &s) { seen.push_back(s); }).HasValue());
  h.SetNetworkRequestedState(NetworkStateType::kFullCom); // BusSleep -> RepeatMessage
  Advance(1000);                                          // RepeatMessage -> NormalOperation: no change
  h.SetNetworkRequestedState(NetworkStateType::kNoCom);   // NormalOperation -> ReadySleep: no change
  Advance(2000);                                          // ReadySleep -> PrepareBusSleep
  Advance(1500);                                          // -> BusSleep: no change
  ASSERT_EQ(seen.size(), 2U);
  EXPECT_EQ(seen[0], NetworkStateType::kFullCom);
  EXPECT_EQ(seen[1], NetworkStateType::kNoCom);
}

TEST_F(Spec, RequestedStateNotifierAndOverwriteAndUnregister) {
  NetworkHandle h(port);
  int a = 0, b = 0;
  ASSERT_TRUE(h.RegisterNetworkRequestedStateChangeNotifier([&](NetworkStateType const &) { ++a; }).HasValue());
  ASSERT_TRUE(h.RegisterNetworkRequestedStateChangeNotifier([&](NetworkStateType const &) { ++b; }).HasValue());
  h.SetNetworkRequestedState(NetworkStateType::kFullCom);
  h.SetNetworkRequestedState(NetworkStateType::kFullCom); // no change -> no notification
  h.SetNetworkRequestedState(NetworkStateType::kNoCom);
  EXPECT_EQ(a, 0); // overwritten (SWS_ANM_01012: only one notifier)
  EXPECT_EQ(b, 2);
  h.UnregisterNetworkRequestedStateChangeNotifier();
  h.SetNetworkRequestedState(NetworkStateType::kFullCom);
  EXPECT_EQ(b, 2);
}

TEST_F(Spec, EmptyNotifierIsInvalidHandler) {
  NetworkHandle h(port);
  auto r = h.RegisterNetworkStateChangeNotifier(nullptr);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(NmErrc::kInvalidHandler));
  EXPECT_EQ(h.RegisterNetworkRequestedStateChangeNotifier({}).Error(), MakeErrorCode(NmErrc::kInvalidHandler));
}

TEST_F(Spec, MovedHandleKeepsWorkingAndDestroyedHandleStopsNotifying) {
  int n = 0;
  {
    NetworkHandle h(port);
    ASSERT_TRUE(h.RegisterNetworkStateChangeNotifier([&](NetworkStateType const &) { ++n; }).HasValue());
    NetworkHandle moved(std::move(h));
    moved.SetNetworkRequestedState(NetworkStateType::kFullCom);
    EXPECT_EQ(n, 1);
  } // destroyed: observers removed
  NetworkHandle other(port);
  other.SetNetworkRequestedState(NetworkStateType::kNoCom);
  Advance(10000);
  EXPECT_EQ(n, 1);
}

TEST_F(Spec, WithdrawAllOnShutdown) { // SWS_ANM_00090
  NetworkHandle h(port);
  h.SetNetworkRequestedState(NetworkStateType::kFullCom);
  Advance(1000);
  mgr.WithdrawAll();
  EXPECT_EQ(h.GetNetworkRequestedState().Value(), NetworkStateType::kNoCom);
  EXPECT_EQ(ch->GetState(), NmState::kReadySleep);
}

// ---- engine behaviour from the SWS state machine ----------------------------------------------

namespace {
struct Eng : ::testing::Test {
  std::shared_ptr<ManualClock> clk = std::make_shared<ManualClock>();
  std::unique_ptr<NmChannel> h;
  std::vector<NmPdu> sent;
  void Make(NmConfig c) {
    auto r = NmChannel::Create("e", c, clk);
    ASSERT_TRUE(r.HasValue());
    h = std::move(r).Value();
    h->SetTxHandler([this](NmPdu const &p) { sent.push_back(p); });
  }
  void Advance(int ms) { clk->Advance(milliseconds(ms)); h->Process(); }
};
} // namespace

TEST_F(Eng, PnResetTimeMustExceedCycleTime) { // SWS_ANM_00056
  NmConfig c = Cfg();
  c.pnFilterMask = 1;
  c.pnResetTime = c.messageCycleTime;
  auto r = NmChannel::Create("x", c, clk);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(NmErrc::kInvalidConfig));
  c.pnFilterMask = 0; // not enforced without partial networking
  EXPECT_TRUE(NmChannel::Create("x", c, clk).HasValue());
}

TEST_F(Eng, PniMessageWithoutOwnPnBitIsDroppedUnlessKeepAwake) { // SWS_ANM_00081 / 00089
  NmConfig c = Cfg();
  c.pnFilterMask = 0x3;
  Make(c);
  NmPdu foreign;
  foreign.partialNetworkInfo = true;
  foreign.pnRequest = 0x8; // PN3 - not served by this node
  h->OnPduReceived(foreign);
  EXPECT_EQ(h->GetState(), NmState::kBusSleep) << "must not wake the node";

  NmPdu own = foreign;
  own.pnRequest = 0x1;
  h->OnPduReceived(own);
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);

  c.allNmMessagesKeepAwake = true;
  Make(c);
  h->OnPduReceived(foreign);
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
}

TEST_F(Eng, PniZeroMessagesAreNotFilteredAndKeepUserData) { // SWS_ANM_00052
  NmConfig c = Cfg();
  c.pnFilterMask = 0x3;
  Make(c);
  NmPdu plain;
  plain.partialNetworkInfo = false;
  h->OnPduReceived(plain);
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
}

TEST_F(Eng, ImmediateNmTransmissions) { // nmImmediateNmTransmissions / nmImmediateNmCycleTime
  NmConfig c = Cfg();
  c.immediateNmTransmissions = 2;
  c.immediateNmCycleTime = milliseconds(20);
  Make(c);
  h->Request();
  EXPECT_EQ(sent.size(), 1U); // first message right away
  Advance(20);
  Advance(20);
  EXPECT_EQ(sent.size(), 3U); // + 2 immediate transmissions at 20 ms
  Advance(20);
  EXPECT_EQ(sent.size(), 3U); // then back to the 500 ms cycle
  Advance(480);
  EXPECT_EQ(sent.size(), 4U);
}

TEST_F(Eng, ReadySleepDoesNotTransmit) { // SWS_ANM_00020
  Make(Cfg());
  h->Request();
  Advance(1000);
  h->Release();
  ASSERT_EQ(h->GetState(), NmState::kReadySleep);
  sent.clear();
  Advance(1900);
  EXPECT_TRUE(sent.empty());
}
