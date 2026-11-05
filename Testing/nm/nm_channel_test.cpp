#include <gtest/gtest.h>

#include <vector>

#include "ara/nm/nm_channel.h"
#include "ara/nm/network_manager.h"
#include "ara/nm/nm_error_domain.h"

using namespace ara::nm;
using std::chrono::milliseconds;

namespace {
NmConfig Cfg() {
  NmConfig c;
  c.nodeId = 3;
  c.repeatMessageTime = milliseconds(1000);
  c.nmTimeout = milliseconds(2000);
  c.waitBusSleepTime = milliseconds(1500);
  c.messageCycleTime = milliseconds(500);
  c.pnResetTime = milliseconds(3000);
  c.pnFilterMask = 0x0F;
  return c;
}

struct Fx : ::testing::Test {
  std::shared_ptr<ManualClock> clk = std::make_shared<ManualClock>();
  std::unique_ptr<NmChannel> h;
  std::vector<std::pair<NmState, NmState>> changes;
  std::vector<NmPdu> sent;

  void SetUp() override { Make(Cfg()); }
  void Make(NmConfig c) {
    auto r = NmChannel::Create("can0", c, clk);
    ASSERT_TRUE(r.HasValue());
    h = std::move(r).Value();
    h->SetStateChangeHandler([this](NmState a, NmState b) { changes.emplace_back(a, b); });
    h->SetTxHandler([this](NmPdu const &p) { sent.push_back(p); });
  }
  void Advance(int ms) {
    clk->Advance(milliseconds(ms));
    h->Process();
  }
};
} // namespace

TEST_F(Fx, StartsInBusSleepAndStaysQuiet) {
  EXPECT_EQ(h->GetState(), NmState::kBusSleep);
  Advance(10000);
  EXPECT_EQ(h->GetState(), NmState::kBusSleep);
  EXPECT_TRUE(sent.empty());
}

TEST_F(Fx, RequestEntersRepeatMessageAndSendsImmediately) {
  ASSERT_TRUE(h->Request().HasValue());
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
  ASSERT_EQ(sent.size(), 1U);
  EXPECT_FALSE(sent[0].repeatMessageRequest); // SWS_ANM_00037: transmitted RMR bit is always 0
  EXPECT_EQ(sent[0].sourceNodeId, 3);
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0], std::make_pair(NmState::kBusSleep, NmState::kRepeatMessage));
}

TEST_F(Fx, RepeatMessageThenNormalOperationWhenRequested) {
  h->Request();
  Advance(999);
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
  Advance(1);
  EXPECT_EQ(h->GetState(), NmState::kNormalOperation);
}

TEST_F(Fx, PeriodicTransmission) {
  h->Request();
  for (int i = 0; i < 4; ++i) Advance(500);
  // t=0 initial, then 500,1000,1500,2000
  EXPECT_EQ(sent.size(), 5U);
  EXPECT_FALSE(sent.back().repeatMessageRequest); // NormalOperation clears RMR
}

TEST_F(Fx, ReleaseInNormalOperationGoesToReadySleepAndStopsSending) {
  h->Request();
  Advance(1000);
  auto n = sent.size();
  ASSERT_TRUE(h->Release().HasValue());
  EXPECT_EQ(h->GetState(), NmState::kReadySleep);
  Advance(1500);
  EXPECT_EQ(sent.size(), n);
}

TEST_F(Fx, FullSleepSequence) {
  h->Request();
  Advance(1000);
  h->Release();
  Advance(2000);
  EXPECT_EQ(h->GetState(), NmState::kPrepareBusSleep);
  Advance(1500);
  EXPECT_EQ(h->GetState(), NmState::kBusSleep);
  std::vector<NmState> to;
  for (auto &c : changes) to.push_back(c.second);
  EXPECT_EQ(to, (std::vector<NmState>{NmState::kRepeatMessage, NmState::kNormalOperation,
                                       NmState::kReadySleep, NmState::kPrepareBusSleep,
                                       NmState::kBusSleep}));
}

TEST_F(Fx, SingleLargeJumpWalksAllTransitions) {
  h->Request();
  h->Release();
  Advance(60000);
  EXPECT_EQ(h->GetState(), NmState::kBusSleep);
}

TEST_F(Fx, ReleaseDuringRepeatMessageWaitsForTimer) {
  h->Request();
  h->Release();
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
  Advance(1000);
  EXPECT_EQ(h->GetState(), NmState::kReadySleep);
}

TEST_F(Fx, ReleaseWithoutRequestIsAnError) {
  auto r = h->Release();
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(NmErrc::kNotRequested));
}

TEST_F(Fx, RequestIsIdempotent) {
  h->Request();
  h->Request();
  EXPECT_EQ(changes.size(), 1U);
}

TEST_F(Fx, RequestInReadySleepReturnsToNormalOperation) {
  h->Request();
  Advance(1000);
  h->Release();
  h->Request();
  EXPECT_EQ(h->GetState(), NmState::kNormalOperation);
}

TEST_F(Fx, RequestInPrepareBusSleepWakesNetwork) {
  h->Request();
  Advance(1000);
  h->Release();
  Advance(2000);
  ASSERT_EQ(h->GetState(), NmState::kPrepareBusSleep);
  h->Request();
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
}

TEST_F(Fx, RemoteMessageWakesBusSleep) {
  NmPdu p;
  p.sourceNodeId = 9;
  h->OnPduReceived(p);
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
  EXPECT_FALSE(h->IsRequested());
  Advance(1000);
  EXPECT_EQ(h->GetState(), NmState::kReadySleep); // nobody here asked for the bus
}

TEST_F(Fx, ReceivedMessagesKeepReadySleepAlive) {
  NmPdu p;
  h->OnPduReceived(p);
  Advance(1000);
  ASSERT_EQ(h->GetState(), NmState::kReadySleep);
  for (int i = 0; i < 5; ++i) {
    Advance(1500);
    h->OnPduReceived(p);
  }
  EXPECT_EQ(h->GetState(), NmState::kReadySleep);
  Advance(2000);
  EXPECT_EQ(h->GetState(), NmState::kPrepareBusSleep);
}

TEST_F(Fx, RemoteRepeatMessageRequestRestartsRepeatMessage) {
  h->Request();
  Advance(1000);
  ASSERT_EQ(h->GetState(), NmState::kNormalOperation);
  NmPdu p;
  p.repeatMessageRequest = true;
  h->OnPduReceived(p);
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
  Advance(1000);
  EXPECT_EQ(h->GetState(), NmState::kNormalOperation);
}

TEST_F(Fx, PlainMessageDoesNotLeaveNormalOperation) {
  h->Request();
  Advance(1000);
  h->OnPduReceived(NmPdu{});
  EXPECT_EQ(h->GetState(), NmState::kNormalOperation);
}

TEST_F(Fx, RawBytesPath) {
  std::uint8_t bad[3] = {};
  EXPECT_FALSE(h->OnBytesReceived(bad, 3).HasValue());
  EXPECT_EQ(h->GetState(), NmState::kBusSleep);
  auto bytes = NmPdu{}.Encode();
  EXPECT_TRUE(h->OnBytesReceived(bytes.data(), bytes.size()).HasValue());
  EXPECT_EQ(h->GetState(), NmState::kRepeatMessage);
}

TEST_F(Fx, LocalPartialNetworkIsSentFilteredByMask) {
  h->RequestPartialNetwork(1);
  h->RequestPartialNetwork(20); // outside the mask: never transmitted
  h->Request();
  ASSERT_FALSE(sent.empty());
  EXPECT_TRUE(sent[0].partialNetworkInfo);
  EXPECT_EQ(sent[0].pnRequest, 0x2U);
  EXPECT_TRUE(h->IsPartialNetworkRequested(20).Value());
  h->ReleasePartialNetwork(20);
  EXPECT_FALSE(h->IsPartialNetworkRequested(20).Value());
}

TEST_F(Fx, RemotePartialNetworkExpires) {
  NmPdu p;
  p.partialNetworkInfo = true;
  p.pnRequest = 0x1 | 0x100; // PN 0 is ours, PN 8 is filtered out
  h->OnPduReceived(p);
  EXPECT_TRUE(h->IsPartialNetworkRequested(0).Value());
  EXPECT_FALSE(h->IsPartialNetworkRequested(8).Value());
  Advance(2999);
  EXPECT_TRUE(h->IsPartialNetworkRequested(0).Value());
  Advance(1);
  EXPECT_FALSE(h->IsPartialNetworkRequested(0).Value());
}

TEST_F(Fx, PartialNetworkIdOutOfRange) {
  EXPECT_FALSE(h->RequestPartialNetwork(32).HasValue());
  EXPECT_FALSE(h->ReleasePartialNetwork(32).HasValue());
  EXPECT_FALSE(h->IsPartialNetworkRequested(40).HasValue());
}

TEST_F(Fx, NoPnInfoWhenMaskIsEmpty) {
  auto c = Cfg();
  c.pnFilterMask = 0;
  Make(c);
  h->Request();
  ASSERT_FALSE(sent.empty());
  EXPECT_FALSE(sent[0].partialNetworkInfo);
}

TEST_F(Fx, HandlerMayReenterHandle) {
  h->SetStateChangeHandler([this](NmState, NmState to) {
    if (to == NmState::kRepeatMessage) (void)h->GetState();
  });
  h->Request(); // would deadlock if callbacks ran under the lock
  SUCCEED();
}

TEST(NmConfigCheck, ZeroTimerRejected) {
  auto clk = std::make_shared<ManualClock>();
  auto c = Cfg();
  c.messageCycleTime = milliseconds(0);
  auto r = NmChannel::Create("x", c, clk);
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(NmErrc::kInvalidConfig));
  EXPECT_FALSE(NmChannel::Create("x", Cfg(), nullptr).HasValue());
}

TEST(NmStateNames, ToStringCoversAll) {
  EXPECT_STREQ(ToString(NmState::kBusSleep), "BusSleep");
  EXPECT_STREQ(ToString(NmState::kRepeatMessage), "RepeatMessage");
  EXPECT_STREQ(ToString(NmState::kReadySleep), "ReadySleep");
}

TEST(NetworkManagerTest, CreateGetProcess) {
  auto clk = std::make_shared<ManualClock>();
  NetworkManager m(clk);
  auto a = m.CreateNetwork("can0", Cfg());
  ASSERT_TRUE(a.HasValue());
  EXPECT_EQ(m.CreateNetwork("can0", Cfg()).Error(), MakeErrorCode(NmErrc::kNetworkExists));
  auto bad = Cfg();
  bad.nmTimeout = milliseconds(0);
  EXPECT_EQ(m.CreateNetwork("can1", bad).Error(), MakeErrorCode(NmErrc::kInvalidConfig));
  EXPECT_EQ(m.GetNetwork("nope").Error(), MakeErrorCode(NmErrc::kUnknownNetwork));
  EXPECT_EQ(m.GetNetwork("can0").Value(), a.Value());
  EXPECT_EQ(m.Size(), 1U);
  a.Value()->Request();
  a.Value()->Release();
  clk->Advance(milliseconds(60000));
  m.ProcessAll();
  EXPECT_EQ(a.Value()->GetState(), NmState::kBusSleep);
}
