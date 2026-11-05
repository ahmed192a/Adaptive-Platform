// Conformance tests for AUTOSAR AP R25-11 SWS_TimeSynchronization
// (see doc/spec-conformance/tsync.md).
#include <gtest/gtest.h>

#include "ara/tsync/synchronized_time_base.h"
#include "ara/tsync/tsync_error_domain.h"

using namespace ara::tsync;
using namespace std::chrono;
using Clk = steady_clock;

namespace {
Timestamp T(milliseconds ms) { return Timestamp(duration_cast<Duration>(ms)); }
std::int64_t Ms(Timestamp t) { return duration_cast<milliseconds>(t.time_since_epoch()).count(); }

struct Spec : ::testing::Test {
  Clk::time_point now{Clk::time_point{} + hours(1)};
  std::shared_ptr<TimeBaseResource> tb;
  ara::core::InstanceSpecifier port{"Vehicle/GlobalTime"};

  void SetUp() override { Make({}); }
  void TearDown() override { TimeBaseRegistry::Unbind("Vehicle/GlobalTime"); }
  void Make(TimeBaseConfig c) {
    c.clock = [this] { return now; };
    auto r = TimeBaseResource::Create(std::move(c));
    ASSERT_TRUE(r.HasValue());
    tb = r.Value();
    TimeBaseRegistry::Bind("Vehicle/GlobalTime", tb);
  }
  void Advance(milliseconds d) { now += d; }
};
} // namespace

TEST(TsyncSpecEnums, ValuesMatchSws) { // SWS_TS_01050 / 01051 (R23-11 listing)
  EXPECT_EQ(static_cast<std::uint32_t>(SynchronizationStatus::kNotSynchronizedUntilStartup), 0U);
  EXPECT_EQ(static_cast<std::uint32_t>(SynchronizationStatus::kTimeOut), 1U);
  EXPECT_EQ(static_cast<std::uint32_t>(SynchronizationStatus::kSynchronized), 2U);
  EXPECT_EQ(static_cast<std::uint32_t>(SynchronizationStatus::kSynchToGateway), 3U);
  EXPECT_EQ(static_cast<std::uint32_t>(LeapJump::kTimeLeapNone), 0U);
  EXPECT_EQ(static_cast<std::uint32_t>(LeapJump::kTimeLeapFuture), 1U);
  EXPECT_EQ(static_cast<std::uint32_t>(LeapJump::kTimeLeapPast), 2U);
}

TEST(TsyncSpecErrors, DomainIdDiffersFromNmAndCodesExist) {
  EXPECT_NE(TsyncErrorDomain::kId, 0x8000000000000501ULL); // was shared with ara::nm
  auto ec = MakeErrorCode(TsyncErrc::kLimitsExceeded);
  EXPECT_STREQ(ec.Domain().Name(), "Tsync");
  EXPECT_STREQ(MakeErrorCode(TsyncErrc::kFunctionNotSupported).Message(), "function not supported");
  EXPECT_STREQ(MakeErrorCode(TsyncErrc::kDaemonConnectionLost).Message(), "time sync daemon connection lost");
}

TEST_F(Spec, UnboundConsumerAndProviderBehaveAsDaemonLost) {
  TimeBaseRegistry::Unbind("Vehicle/GlobalTime");
  SynchronizedTimeBaseConsumer c(port);
  EXPECT_EQ(c.GetTimeWithStatus().GetSynchronizationStatus(), SynchronizationStatus::kNotSynchronizedUntilStartup);
  EXPECT_EQ(c.GetRateDeviation(), 0.0);
  EXPECT_EQ(c.RegisterStatusChangeNotifier([](SynchronizedTimeBaseStatus const &) {}).Error(),
            MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  SynchronizedTimeBaseProvider p(port);
  EXPECT_EQ(p.SetTime(T(milliseconds(1))).Error(), MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  EXPECT_TRUE(p.GetUserData().empty());
}

TEST_F(Spec, InitialStatusIsNotSynchronizedUntilStartup) { // SWS_TS_00032
  SynchronizedTimeBaseConsumer c(port);
  auto st = c.GetTimeWithStatus();
  EXPECT_EQ(st.GetSynchronizationStatus(), SynchronizationStatus::kNotSynchronizedUntilStartup);
  EXPECT_EQ(st.GetLeapJump(), LeapJump::kTimeLeapNone); // SWS_TS_00064
  EXPECT_EQ(c.GetRateDeviation(), 0.0);                 // SWS_TS_00070
}

TEST_F(Spec, SetTimeSynchronizesAndTimeAdvances) {
  SynchronizedTimeBaseProvider p(port);
  SynchronizedTimeBaseConsumer c(port);
  ASSERT_TRUE(p.SetTime(T(seconds(100))).HasValue());
  EXPECT_EQ(c.GetTimeWithStatus().GetSynchronizationStatus(), SynchronizationStatus::kSynchronized);
  Advance(milliseconds(250));
  EXPECT_EQ(Ms(c.GetTimeWithStatus().GetCreationTime()), 100250);
  EXPECT_EQ(Ms(p.GetCurrentTime()), 100250);
}

TEST_F(Spec, TimeoutAndGatewayStatus) {
  SynchronizedTimeBaseProvider p(port);
  SynchronizedTimeBaseConsumer c(port);
  p.SetTime(T(seconds(1)));
  Advance(seconds(2)); // default timeout 1 s
  EXPECT_EQ(c.GetTimeWithStatus().GetSynchronizationStatus(), SynchronizationStatus::kTimeOut);
  p.UpdateTime(T(seconds(3)));
  EXPECT_EQ(c.GetTimeWithStatus().GetSynchronizationStatus(), SynchronizationStatus::kSynchronized);
}

TEST_F(Spec, SynchToGatewayMapsToItsOwnStatus) {
  TimeBaseProvider internal(tb);
  internal.Update(T(seconds(1)), 0.0, /*syncToGateway=*/true);
  SynchronizedTimeBaseConsumer c(port);
  EXPECT_EQ(c.GetTimeWithStatus().GetSynchronizationStatus(), SynchronizationStatus::kSynchToGateway);
}

TEST_F(Spec, LeapJumpDirectionIsReported) { // SWS_TS_00027
  SynchronizedTimeBaseProvider p(port);
  SynchronizedTimeBaseConsumer c(port);
  p.SetTime(T(seconds(100)));
  p.UpdateTime(T(seconds(110))); // 10 s ahead
  EXPECT_EQ(c.GetTimeWithStatus().GetLeapJump(), LeapJump::kTimeLeapFuture);
  p.UpdateTime(T(seconds(50)));  // back in time
  EXPECT_EQ(c.GetTimeWithStatus().GetLeapJump(), LeapJump::kTimeLeapPast);
  p.UpdateTime(T(seconds(50)));  // small update clears the leap
  EXPECT_EQ(c.GetTimeWithStatus().GetLeapJump(), LeapJump::kTimeLeapNone);
}

TEST_F(Spec, UserDataRoundTripAndEmptyWhenAbsent) { // SWS_TS_00120 / 14219
  SynchronizedTimeBaseProvider p(port);
  SynchronizedTimeBaseConsumer c(port);
  p.SetTime(T(seconds(1)));
  EXPECT_TRUE(c.GetTimeWithStatus().GetUserData().empty());
  std::uint8_t const ud[3] = {1, 2, 3};
  ASSERT_TRUE(p.SetUserData(ud, 3).HasValue());
  EXPECT_EQ(p.GetUserData(), (std::vector<std::uint8_t>{1, 2, 3}));
  EXPECT_EQ(c.GetTimeWithStatus().GetUserData(), (std::vector<std::uint8_t>{1, 2, 3}));
  EXPECT_EQ(p.SetUserData(ud, 4).Error(), MakeErrorCode(TsyncErrc::kInvalidArgument));
}

TEST_F(Spec, RateCorrectionNotSupportedByDefault) { // SWS_TS_00061
  SynchronizedTimeBaseProvider p(port);
  EXPECT_EQ(p.SetRateCorrection(0.0001).Error(), MakeErrorCode(TsyncErrc::kFunctionNotSupported));
}

TEST_F(Spec, RateCorrectionLimitsAndEffect) { // SWS_TS_00062 / 00063
  TimeBaseConfig c;
  c.allowProviderRateCorrection = true;
  c.providerRateDeviationMax = 0.001;
  c.timeout = seconds(100);
  Make(c);
  SynchronizedTimeBaseProvider p(port);
  p.SetTime(T(seconds(0)));
  EXPECT_EQ(p.SetRateCorrection(0.01).Error(), MakeErrorCode(TsyncErrc::kLimitsExceeded));
  EXPECT_EQ(p.SetRateCorrection(-0.01).Error(), MakeErrorCode(TsyncErrc::kLimitsExceeded));
  ASSERT_TRUE(p.SetRateCorrection(0.001).HasValue());
  EXPECT_DOUBLE_EQ(p.GetRateDeviation(), 0.001);
  Advance(seconds(10));
  EXPECT_EQ(Ms(p.GetCurrentTime()), 10010); // runs 0.1 % fast
}

TEST_F(Spec, NotifiersFireForStatusSyncStateAndLeap) { // SWS_TS_00701 / 00702 / 00703
  SynchronizedTimeBaseProvider p(port);
  SynchronizedTimeBaseConsumer c(port);
  std::vector<SynchronizationStatus> syncs;
  std::vector<LeapJump> leaps;
  int statusCalls = 0;
  ASSERT_TRUE(c.RegisterStatusChangeNotifier([&](SynchronizedTimeBaseStatus const &) { ++statusCalls; }).HasValue());
  ASSERT_TRUE(c.RegisterSynchronizationStateChangeNotifier([&](SynchronizationStatus s) { syncs.push_back(s); }).HasValue());
  ASSERT_TRUE(c.RegisterTimeLeapNotifier([&](LeapJump l) { leaps.push_back(l); }).HasValue());
  p.SetTime(T(seconds(1)));      // -> synchronized
  p.UpdateTime(T(seconds(30)));  // leap into the future
  Advance(seconds(2));
  tb->Poll();                    // timeout detected
  ASSERT_GE(syncs.size(), 2U);
  EXPECT_EQ(syncs.front(), SynchronizationStatus::kSynchronized);
  EXPECT_EQ(syncs.back(), SynchronizationStatus::kTimeOut);
  ASSERT_EQ(leaps.size(), 1U);
  EXPECT_EQ(leaps[0], LeapJump::kTimeLeapFuture);
  EXPECT_GE(statusCalls, 2);

  c.UnregisterTimeLeapNotifier();
  p.UpdateTime(T(seconds(500)));
  EXPECT_EQ(leaps.size(), 1U);
}

TEST_F(Spec, ConsumerDestructionStopsNotifications) {
  SynchronizedTimeBaseProvider p(port);
  int n = 0;
  {
    SynchronizedTimeBaseConsumer c(port);
    c.RegisterStatusChangeNotifier([&](SynchronizedTimeBaseStatus const &) { ++n; });
    p.SetTime(T(seconds(1)));
  }
  int const before = n;
  p.UpdateTime(T(seconds(900)));
  Advance(seconds(5));
  tb->Poll();
  EXPECT_EQ(n, before);
}

TEST_F(Spec, MovedConsumerKeepsWorking) {
  SynchronizedTimeBaseProvider p(port);
  SynchronizedTimeBaseConsumer a(port);
  SynchronizedTimeBaseConsumer b(std::move(a));
  p.SetTime(T(seconds(7)));
  EXPECT_EQ(Ms(b.GetTimeWithStatus().GetCreationTime()), 7000);
}
