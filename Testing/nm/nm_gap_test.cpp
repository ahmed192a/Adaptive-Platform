// Gap-closing tests: configurable PDU layout, nmMsgCycleOffset, multi-channel aggregation,
// executor overloads and the SIGTERM hook (doc/spec-conformance/nm.md).
#include <gtest/gtest.h>

#include <csignal>
#include <functional>
#include <vector>

#include "ara/nm/network_handle.h"
#include "ara/nm/network_manager.h"
#include "ara/nm/nm_error_domain.h"

using namespace ara::nm;
using std::chrono::milliseconds;

namespace {
NmConfig Cfg() {
  NmConfig c;
  c.nodeId = 7;
  c.repeatMessageTime = milliseconds(1000);
  c.nmTimeout = milliseconds(2000);
  c.waitBusSleepTime = milliseconds(1500);
  c.messageCycleTime = milliseconds(500);
  c.pnResetTime = milliseconds(3000);
  return c;
}
} // namespace

// ---------------------------------------------------------------------------------- PDU layout
TEST(NmLayout, DefaultLayoutIsValidAndMatchesFixedEncoding) {
  NmPduLayout l;
  EXPECT_TRUE(l.IsValid());
  EXPECT_EQ(l.UserDataLength(), 2U);
  NmPdu p;
  p.sourceNodeId = 9;
  p.partialNetworkInfo = true;
  p.pnRequest = 0x01020304;
  p.userData = {0xAA, 0xBB};
  auto fixed = p.Encode();
  auto cfg = p.Encode(l);
  ASSERT_TRUE(cfg.HasValue());
  EXPECT_EQ(std::vector<std::uint8_t>(fixed.begin(), fixed.end()), cfg.Value());
}

TEST(NmLayout, RejectsInconsistentLayouts) {
  NmPduLayout l;
  l.nidPosition = 1; // collides with the CBV at byte 1
  EXPECT_FALSE(l.IsValid());
  l = {};
  l.pduLength = 1;
  EXPECT_FALSE(l.IsValid());
  l = {};
  l.pnVectorOffset = 6; // 6 + 4 > 8
  EXPECT_FALSE(l.IsValid());
  l = {};
  l.pnVectorOffset = 0; // overlaps node id and CBV
  EXPECT_FALSE(l.IsValid());
  l = {};
  l.cbvPosition = NmPduLayout::kAbsent; // PNI bit needs a CBV
  EXPECT_FALSE(l.IsValid());
  l.pnVectorLength = 0;
  EXPECT_TRUE(l.IsValid());
  l = {};
  l.pduLength = 65;
  EXPECT_FALSE(l.IsValid());
}

TEST(NmLayout, CustomLayoutRoundTripWithLongUserData) {
  NmPduLayout l;
  l.pduLength = 12;
  l.nidPosition = 1;
  l.cbvPosition = 0;
  l.pnVectorOffset = 2;
  l.pnVectorLength = 2;
  ASSERT_TRUE(l.IsValid());
  EXPECT_EQ(l.UserDataLength(), 8U);
  NmPdu p;
  p.sourceNodeId = 0x42;
  p.repeatMessageRequest = true;
  p.partialNetworkInfo = true;
  p.pnRequest = 0x0A05;
  p.userData = {1, 2};
  p.moreUserData = {3, 4, 5, 6, 7, 8};
  auto enc = p.Encode(l);
  ASSERT_TRUE(enc.HasValue());
  ASSERT_EQ(enc.Value().size(), 12U);
  EXPECT_EQ(enc.Value()[1], 0x42); // node id at byte 1
  EXPECT_EQ(enc.Value()[2], 0x05); // PN vector little endian at bytes 2..3
  EXPECT_EQ(enc.Value()[3], 0x0A);
  auto dec = NmPdu::Decode(enc.Value().data(), enc.Value().size(), l);
  ASSERT_TRUE(dec.HasValue());
  EXPECT_EQ(dec.Value(), p);
}

TEST(NmLayout, WrongLengthAndBadPnAreRejected) {
  NmPduLayout l;
  l.pduLength = 6;
  l.pnVectorLength = 2;
  ASSERT_TRUE(l.IsValid());
  std::vector<std::uint8_t> eight(8, 0);
  EXPECT_EQ(NmPdu::Decode(eight.data(), eight.size(), l).Error(), MakeErrorCode(NmErrc::kBadPdu));
  std::vector<std::uint8_t> six(6, 0);
  six[2] = 0x01; // PN bit without PNI
  EXPECT_EQ(NmPdu::Decode(six.data(), six.size(), l).Error(), MakeErrorCode(NmErrc::kBadPdu));
  NmPduLayout bad;
  bad.pduLength = 1;
  EXPECT_EQ(NmPdu{}.Encode(bad).Error(), MakeErrorCode(NmErrc::kInvalidConfig));
}

TEST(NmLayout, NoNodeIdNoCbvLayout) {
  NmPduLayout l;
  l.pduLength = 4;
  l.nidPosition = NmPduLayout::kAbsent;
  l.cbvPosition = NmPduLayout::kAbsent;
  l.pnVectorLength = 0;
  ASSERT_TRUE(l.IsValid());
  NmPdu p;
  p.userData = {0x11, 0x22};
  p.moreUserData = {0x33, 0x44};
  p.repeatMessageRequest = true; // cannot be carried without a CBV
  auto enc = p.Encode(l);
  ASSERT_TRUE(enc.HasValue());
  EXPECT_EQ(enc.Value(), (std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0x44}));
  auto dec = NmPdu::Decode(enc.Value().data(), 4, l);
  ASSERT_TRUE(dec.HasValue());
  EXPECT_FALSE(dec.Value().repeatMessageRequest);
}

TEST(NmLayout, ChannelUsesConfiguredLayoutOnTheWire) {
  auto clk = std::make_shared<ManualClock>();
  NetworkManager mgr(clk);
  NmConfig c = Cfg();
  c.pduLayout.pduLength = 12;
  c.pduLayout.nidPosition = 1;
  c.pduLayout.cbvPosition = 0;
  c.pduLayout.pnVectorOffset = 2;
  c.pduLayout.pnVectorLength = 2;
  c.pnFilterMask = 0x3;
  auto ch = mgr.CreateNetwork("fd0", c).Value();
  std::vector<NmPdu> tx;
  ch->SetTxHandler([&](NmPdu const &p) { tx.push_back(p); });
  ASSERT_TRUE(ch->Request().HasValue());
  ch->Process();
  ASSERT_FALSE(tx.empty());
  auto wire = ch->EncodeTx(tx.front());
  ASSERT_TRUE(wire.HasValue());
  EXPECT_EQ(wire.Value().size(), 12U);
  EXPECT_EQ(wire.Value()[1], 7); // node id
  // A peer message in the same layout wakes the channel from the wire bytes.
  auto peer = NmChannel::Create("peer", c, clk).Value();
  NmPdu in;
  in.sourceNodeId = 3;
  in.repeatMessageRequest = true;
  auto bytes = in.Encode(c.pduLayout).Value();
  EXPECT_TRUE(peer->OnBytesReceived(bytes.data(), bytes.size()).HasValue());
  EXPECT_EQ(peer->GetState(), NmState::kRepeatMessage);
  std::vector<std::uint8_t> eight(8, 0);
  EXPECT_EQ(peer->OnBytesReceived(eight.data(), eight.size()).Error(), MakeErrorCode(NmErrc::kBadPdu));
}

TEST(NmLayout, CreateRejectsLayoutThatCannotCarryThePartialNetworks) {
  auto clk = std::make_shared<ManualClock>();
  NmConfig c = Cfg();
  c.pnFilterMask = 0x100; // PN 8 needs 2 bytes
  c.pduLayout.pnVectorLength = 1;
  EXPECT_EQ(NmChannel::Create("x", c, clk).Error(), MakeErrorCode(NmErrc::kInvalidConfig));
  c = Cfg();
  c.pduLayout.pduLength = 1;
  EXPECT_EQ(NmChannel::Create("x", c, clk).Error(), MakeErrorCode(NmErrc::kInvalidConfig));
  c = Cfg();
  c.messageCycleOffset = milliseconds(-1);
  EXPECT_EQ(NmChannel::Create("x", c, clk).Error(), MakeErrorCode(NmErrc::kInvalidConfig));
  c = Cfg();
  c.pnFilterMask = 1;
  c.pduLayout.pnVectorLength = 0;
  c.pduLayout.cbvPosition = NmPduLayout::kAbsent;
  EXPECT_EQ(NmChannel::Create("x", c, clk).Error(), MakeErrorCode(NmErrc::kInvalidConfig));
}

// -------------------------------------------------------------------------- nmMsgCycleOffset
TEST(NmCycleOffset, FirstMessageIsDelayedByTheOffset) {
  auto clk = std::make_shared<ManualClock>();
  NmConfig c = Cfg();
  c.messageCycleOffset = milliseconds(120);
  auto ch = NmChannel::Create("c", c, clk).Value();
  std::vector<std::int64_t> times;
  ch->SetTxHandler([&](NmPdu const &) { times.push_back(clk->Now().count()); });
  ASSERT_TRUE(ch->Request().HasValue());
  ch->Process();
  EXPECT_TRUE(times.empty()); // offset not elapsed
  clk->Advance(milliseconds(119));
  ch->Process();
  EXPECT_TRUE(times.empty());
  clk->Advance(milliseconds(1));
  ch->Process();
  ASSERT_EQ(times.size(), 1U);
  EXPECT_EQ(times[0], 120);
  clk->Advance(milliseconds(500));
  ch->Process();
  EXPECT_EQ(times.size(), 2U); // then the normal cycle
}

TEST(NmCycleOffset, ZeroOffsetKeepsImmediateFirstMessage) {
  auto clk = std::make_shared<ManualClock>();
  auto ch = NmChannel::Create("c", Cfg(), clk).Value();
  int n = 0;
  ch->SetTxHandler([&](NmPdu const &) { ++n; });
  ASSERT_TRUE(ch->Request().HasValue());
  ch->Process();
  EXPECT_EQ(n, 1);
}

TEST(NmCycleOffset, ImmediateTransmissionsIgnoreTheOffset) {
  auto clk = std::make_shared<ManualClock>();
  NmConfig c = Cfg();
  c.messageCycleOffset = milliseconds(300);
  c.immediateNmTransmissions = 2;
  auto ch = NmChannel::Create("c", c, clk).Value();
  int n = 0;
  ch->SetTxHandler([&](NmPdu const &) { ++n; });
  ASSERT_TRUE(ch->Request().HasValue());
  ch->Process();
  EXPECT_EQ(n, 1);
}

// ------------------------------------------------------------------------ multi-channel ports
namespace {
struct Multi : ::testing::Test {
  std::shared_ptr<ManualClock> clk = std::make_shared<ManualClock>();
  NetworkManager mgr{clk};
  NmChannel *a{nullptr};
  NmChannel *b{nullptr};
  ara::core::InstanceSpecifier port{"Vehicle/MultiPort"};
  void SetUp() override {
    a = mgr.CreateNetwork("can0", Cfg()).Value();
    b = mgr.CreateNetwork("eth0", Cfg()).Value();
    ASSERT_TRUE(mgr.BindPort("Vehicle/MultiPort", "can0").HasValue());
    ASSERT_TRUE(mgr.BindPort("Vehicle/MultiPort", "eth0").HasValue());
    ASSERT_TRUE(mgr.BindPort("Vehicle/MultiPort", "eth0").HasValue()); // idempotent
  }
  void TearDown() override { NetworkManager::UnbindPort("Vehicle/MultiPort"); }
};
} // namespace

TEST_F(Multi, StateIsLowestOverAllChannels) { // SWS_ANM_00083
  NetworkHandle h(port);
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kNoCom);
  ASSERT_TRUE(a->Request().HasValue());
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kNoCom); // eth0 still No Com
  ASSERT_TRUE(b->Request().HasValue());
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kFullCom);
}

TEST_F(Multi, RequestAppliesToEveryChannelAndReleaseToo) { // SWS_ANM_00084
  NetworkHandle h(port);
  ASSERT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
  EXPECT_TRUE(a->IsRequested());
  EXPECT_TRUE(b->IsRequested());
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kFullCom);
  ASSERT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kNoCom).HasValue());
  EXPECT_FALSE(a->IsRequested());
  EXPECT_FALSE(b->IsRequested());
}

TEST_F(Multi, RequestedStateIsHighest) {
  NetworkHandle h(port);
  ASSERT_TRUE(a->Request().HasValue());
  EXPECT_EQ(h.GetNetworkRequestedState().Value(), NetworkStateType::kFullCom);
}

TEST_F(Multi, NotifierFiresOnlyWhenTheAggregateChanges) {
  NetworkHandle h(port);
  std::vector<NetworkStateType> seen;
  ASSERT_TRUE(h.RegisterNetworkStateChangeNotifier([&](NetworkStateType const &s) { seen.push_back(s); }).HasValue());
  ASSERT_TRUE(a->Request().HasValue());
  EXPECT_TRUE(seen.empty()); // eth0 still No Com -> aggregate unchanged
  ASSERT_TRUE(b->Request().HasValue());
  ASSERT_EQ(seen.size(), 1U);
  EXPECT_EQ(seen[0], NetworkStateType::kFullCom);
}

TEST_F(Multi, RequestedNotifierFollowsTheHighest) {
  NetworkHandle h(port);
  std::vector<NetworkStateType> seen;
  ASSERT_TRUE(
      h.RegisterNetworkRequestedStateChangeNotifier([&](NetworkStateType const &s) { seen.push_back(s); }).HasValue());
  ASSERT_TRUE(a->Request().HasValue());
  ASSERT_TRUE(b->Request().HasValue()); // already FullCom -> no second event
  ASSERT_EQ(seen.size(), 1U);
  ASSERT_TRUE(a->Release().HasValue());
  EXPECT_EQ(seen.size(), 1U); // b still requested
  ASSERT_TRUE(b->Release().HasValue());
  ASSERT_EQ(seen.size(), 2U);
  EXPECT_EQ(seen[1], NetworkStateType::kNoCom);
}

// -------------------------------------------------------------------------- executor overloads
TEST_F(Multi, ExecutorOverloadDeliversThroughTheExecutor) { // SWS_ANM_01014 / 01015
  NetworkHandle h(port);
  std::vector<std::function<void()>> queued;
  int delivered = 0;
  auto executor = [&queued](std::function<void()> task) { queued.push_back(std::move(task)); };
  ASSERT_TRUE(h.RegisterNetworkStateChangeNotifier([&](NetworkStateType const &) { ++delivered; }, executor)
                  .HasValue());
  ASSERT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
  EXPECT_EQ(delivered, 0); // posted, not yet run
  ASSERT_EQ(queued.size(), 1U);
  queued.front()();
  EXPECT_EQ(delivered, 1);
}

TEST_F(Multi, ExecutorOverloadForRequestedAndEmptyNotifier) {
  NetworkHandle h(port);
  std::vector<std::function<void()>> queued;
  auto executor = [&queued](std::function<void()> t) { queued.push_back(std::move(t)); };
  int n = 0;
  ASSERT_TRUE(h.RegisterNetworkRequestedStateChangeNotifier([&](NetworkStateType const &) { ++n; }, executor)
                  .HasValue());
  ASSERT_TRUE(a->Request().HasValue());
  ASSERT_EQ(queued.size(), 1U);
  queued[0]();
  EXPECT_EQ(n, 1);
  auto empty = h.RegisterNetworkStateChangeNotifier(NetworkHandle::NetworkStateChangeNotifier{}, executor);
  ASSERT_FALSE(empty.HasValue());
  EXPECT_EQ(empty.Error(), MakeErrorCode(NmErrc::kInvalidHandler));
}

// ------------------------------------------------------------------------------ SIGTERM hook
TEST(NmSigterm, ShutdownMovesEveryNetworkToNoComAndWithdrawsRequests) {
  auto clk = std::make_shared<ManualClock>();
  NetworkManager mgr(clk);
  auto *ch = mgr.CreateNetwork("can0", Cfg()).Value();
  ASSERT_TRUE(mgr.BindPort("Vehicle/ShutPort", "can0").HasValue());
  NetworkHandle h(ara::core::InstanceSpecifier("Vehicle/ShutPort"));
  ASSERT_TRUE(h.SetNetworkRequestedState(NetworkStateType::kFullCom).HasValue());
  ASSERT_TRUE(ch->RequestPartialNetwork(1).HasValue());
  ASSERT_EQ(h.GetNetworkState().Value(), NetworkStateType::kFullCom);
  NetworkStateType seen = NetworkStateType::kFullCom;
  ASSERT_TRUE(h.RegisterNetworkStateChangeNotifier([&](NetworkStateType const &s) { seen = s; }).HasValue());
  mgr.ShutdownAll();
  EXPECT_EQ(h.GetNetworkState().Value(), NetworkStateType::kNoCom); // immediately, no timers needed
  EXPECT_EQ(seen, NetworkStateType::kNoCom);
  EXPECT_FALSE(ch->IsRequested());
  EXPECT_EQ(ch->GetState(), NmState::kPrepareBusSleep);
  clk->Advance(milliseconds(1500));
  ch->Process();
  EXPECT_EQ(ch->GetState(), NmState::kBusSleep);
  NetworkManager::UnbindPort("Vehicle/ShutPort");
}

TEST(NmSigterm, HookSetsFlagAndServiceSignalsShutsDown) {
  auto clk = std::make_shared<ManualClock>();
  NetworkManager mgr(clk);
  auto *ch = mgr.CreateNetwork("can0", Cfg()).Value();
  ASSERT_TRUE(ch->Request().HasValue());
  ASSERT_TRUE(NetworkManager::InstallSigtermHook().HasValue());
  ASSERT_TRUE(NetworkManager::InstallSigtermHook().HasValue()); // idempotent
  EXPECT_FALSE(NetworkManager::SigtermPending());
  EXPECT_FALSE(mgr.ServiceSignals());
  std::raise(SIGTERM);
  EXPECT_TRUE(NetworkManager::SigtermPending());
  EXPECT_EQ(ch->GetState(), NmState::kRepeatMessage); // handler itself does no work
  EXPECT_TRUE(mgr.ServiceSignals());
  EXPECT_FALSE(NetworkManager::SigtermPending());
  EXPECT_FALSE(ch->IsRequested());
  EXPECT_EQ(ch->GetState(), NmState::kPrepareBusSleep);
  NetworkManager::UninstallSigtermHook();
  NetworkManager::UninstallSigtermHook(); // no-op
}

TEST(NmSigterm, ProcessAllServicesAPendingSigterm) {
  auto clk = std::make_shared<ManualClock>();
  NetworkManager mgr(clk);
  auto *ch = mgr.CreateNetwork("can0", Cfg()).Value();
  ASSERT_TRUE(ch->Request().HasValue());
  ASSERT_TRUE(NetworkManager::InstallSigtermHook().HasValue());
  std::raise(SIGTERM);
  mgr.ProcessAll();
  EXPECT_EQ(ch->GetState(), NmState::kPrepareBusSleep);
  NetworkManager::UninstallSigtermHook();
}
