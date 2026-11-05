#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "ara/tsync/time_base.h"

using namespace ara::tsync;
using namespace std::chrono;
using Clk = steady_clock;

namespace {

SynchronizedTimePoint T(milliseconds ms) { return SynchronizedTimePoint(duration_cast<Duration>(ms)); }
std::int64_t Ms(SynchronizedTimePoint t) { return duration_cast<milliseconds>(t.time_since_epoch()).count(); }

struct TimeBaseTest : ::testing::Test {
  Clk::time_point now{Clk::time_point{} + hours(1)};
  std::shared_ptr<TimeBaseResource> tb;
  std::unique_ptr<TimeBaseProvider> prov;
  std::vector<std::pair<std::uint32_t, ChangeReason>> events;

  void SetUp() override { Make({}); }
  void Make(TimeBaseConfig c) {
    c.name = "GlobalTime";
    c.clock = [this] { return now; };
    auto r = TimeBaseResource::Create(std::move(c));
    ASSERT_TRUE(r.HasValue());
    tb = r.Value();
    prov = std::make_unique<TimeBaseProvider>(tb);
  }
  void Listen(bool every = false) {
    tb->SetUpdateCallback([this](TimeBaseSnapshot const &s, ChangeReason r) { events.emplace_back(s.status.Bits(), r); }, every);
  }
  void Advance(milliseconds d) { now += d; }
};

TEST_F(TimeBaseTest, NotSynchronizedBeforeFirstUpdate) {
  EXPECT_EQ(tb->Name(), "GlobalTime");
  auto r = tb->GetCurrentTime();
  ASSERT_FALSE(r.HasValue());
  EXPECT_EQ(r.Error(), MakeErrorCode(TsyncErrc::kNotSynchronized));
  EXPECT_FALSE(tb->GetRateDeviation().HasValue());
  EXPECT_EQ(tb->GetStatus().Bits(), 0u);
}

TEST_F(TimeBaseTest, FirstUpdateSynchronizes) {
  ASSERT_TRUE(prov->Update(T(milliseconds(5000))).HasValue());
  auto r = tb->GetCurrentTime();
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(Ms(r.Value().time), 5000);
  EXPECT_TRUE(r.Value().status.Test(StatusFlag::kSynchronized));
  EXPECT_FALSE(r.Value().status.Test(StatusFlag::kTimeLeap)); // first sync is not a leap
}

TEST_F(TimeBaseTest, TimeAdvancesWithLocalClock) {
  ASSERT_TRUE(prov->Update(T(milliseconds(1000))).HasValue());
  Advance(milliseconds(250));
  EXPECT_EQ(Ms(tb->GetCurrentTime().Value().time), 1250);
}

TEST_F(TimeBaseTest, RateDeviationScalesElapsedTime) {
  ASSERT_TRUE(prov->Update(T(milliseconds(0)), 0.01).HasValue()); // local clock 1 % fast
  Advance(milliseconds(1000));
  EXPECT_EQ(Ms(tb->GetCurrentTime().Value().time), 1010);
  EXPECT_DOUBLE_EQ(tb->GetRateDeviation().Value(), 0.01);
}

TEST_F(TimeBaseTest, SmallOffsetIsSlewedNotStepped) {
  ASSERT_TRUE(prov->Update(T(milliseconds(1000))).HasValue());
  Advance(milliseconds(100));                                   // time base: 1100
  ASSERT_TRUE(prov->Update(T(milliseconds(1108))).HasValue());  // +8 ms offset (< 10 ms threshold)
  auto s0 = tb->GetCurrentTime().Value();
  EXPECT_EQ(Ms(s0.time), 1100);                                 // continuous at the update instant
  EXPECT_TRUE(s0.status.Test(StatusFlag::kPendingRateCorr));
  EXPECT_FALSE(s0.status.Test(StatusFlag::kTimeLeap));
  Advance(milliseconds(250));                                   // halfway through the 500 ms window
  EXPECT_EQ(Ms(tb->GetCurrentTime().Value().time), 1350 + 4);
  Advance(milliseconds(250));
  auto s1 = tb->GetCurrentTime().Value();
  EXPECT_EQ(Ms(s1.time), 1600 + 8);
  EXPECT_FALSE(s1.status.Test(StatusFlag::kPendingRateCorr));
}

TEST_F(TimeBaseTest, LargeOffsetStepsAndRaisesLeap) {
  Listen();
  ASSERT_TRUE(prov->Update(T(milliseconds(1000))).HasValue());
  events.clear();
  Advance(milliseconds(100));
  ASSERT_TRUE(prov->Update(T(milliseconds(5000))).HasValue());
  auto s = tb->GetCurrentTime().Value();
  EXPECT_EQ(Ms(s.time), 5000);
  EXPECT_TRUE(s.status.Test(StatusFlag::kTimeLeap));
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].second, ChangeReason::kTimeStep);
  // the next in-tolerance update clears the leap flag
  Advance(milliseconds(100));
  ASSERT_TRUE(prov->Update(T(milliseconds(5100))).HasValue());
  EXPECT_FALSE(tb->GetStatus().Test(StatusFlag::kTimeLeap));
}

TEST_F(TimeBaseTest, TimeoutDetectedAndClearedByUpdate) {
  TimeBaseConfig c;
  c.timeout = milliseconds(200);
  Make(c);
  ASSERT_TRUE(prov->Update(T(milliseconds(0))).HasValue());
  Advance(milliseconds(200));
  EXPECT_TRUE(tb->GetStatus().Test(StatusFlag::kSynchronized)); // exactly at the limit
  Advance(milliseconds(1));
  auto st = tb->GetStatus();
  EXPECT_TRUE(st.Test(StatusFlag::kTimeout));
  EXPECT_FALSE(st.Test(StatusFlag::kSynchronized));
  EXPECT_TRUE(tb->GetCurrentTime().HasValue()); // extrapolated time stays available
  ASSERT_TRUE(prov->Update(T(milliseconds(201))).HasValue());
  EXPECT_TRUE(tb->GetStatus().Test(StatusFlag::kSynchronized));
  EXPECT_FALSE(tb->GetStatus().Test(StatusFlag::kTimeout));
}

TEST_F(TimeBaseTest, GatewayFlagFollowsUpdates) {
  ASSERT_TRUE(prov->Update(T(milliseconds(0)), 0.0, true).HasValue());
  EXPECT_TRUE(tb->GetStatus().Test(StatusFlag::kSyncToGateway));
  ASSERT_TRUE(prov->Update(T(milliseconds(0)), 0.0, false).HasValue());
  EXPECT_FALSE(tb->GetStatus().Test(StatusFlag::kSyncToGateway));
}

TEST_F(TimeBaseTest, UserDataIsTransported) {
  UserData ud;
  ud.length = 2;
  ud.bytes[0] = 0xAB;
  ud.bytes[1] = 0xCD;
  ASSERT_TRUE(prov->Update(T(milliseconds(0)), 0.0, false, ud).HasValue());
  auto got = tb->GetUserData();
  EXPECT_EQ(got.length, 2);
  EXPECT_EQ(got.bytes[0], 0xAB);
  EXPECT_EQ(got.bytes[1], 0xCD);
  EXPECT_EQ(tb->GetCurrentTime().Value().userData.bytes[1], 0xCD);
}

TEST_F(TimeBaseTest, CallbackOnStatusChangeAndTimeoutViaPoll) {
  TimeBaseConfig c;
  c.timeout = milliseconds(100);
  Make(c);
  Listen();
  ASSERT_TRUE(prov->Update(T(milliseconds(0))).HasValue()); // none -> SYNCHRONIZED
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].first, static_cast<std::uint32_t>(StatusFlag::kSynchronized));
  ASSERT_TRUE(prov->Update(T(milliseconds(0))).HasValue());  // no change, not subscribed to every update
  EXPECT_EQ(events.size(), 1u);
  tb->Poll();                                                // nothing changed yet
  EXPECT_EQ(events.size(), 1u);
  Advance(milliseconds(150));
  tb->Poll();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].first, static_cast<std::uint32_t>(StatusFlag::kTimeout));
  tb->Poll();                                                // delivered once only
  EXPECT_EQ(events.size(), 2u);
  ASSERT_TRUE(prov->Update(T(milliseconds(150))).HasValue()); // TIMEOUT -> SYNCHRONIZED
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[2].first, static_cast<std::uint32_t>(StatusFlag::kSynchronized));
}

TEST_F(TimeBaseTest, NotifyEveryUpdate) {
  Listen(true);
  ASSERT_TRUE(prov->Update(T(milliseconds(0))).HasValue());
  events.clear();
  ASSERT_TRUE(prov->Update(T(milliseconds(0))).HasValue());
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].second, ChangeReason::kUpdate);
}

TEST_F(TimeBaseTest, InvalidArgumentsRejected) {
  EXPECT_EQ(prov->Update(T(milliseconds(0)), std::numeric_limits<double>::quiet_NaN()).Error(),
            MakeErrorCode(TsyncErrc::kInvalidArgument));
  EXPECT_EQ(prov->Update(T(milliseconds(0)), -1.0).Error(), MakeErrorCode(TsyncErrc::kInvalidArgument));
  UserData bad;
  bad.length = 4;
  EXPECT_EQ(prov->Update(T(milliseconds(0)), 0.0, false, bad).Error(),
            MakeErrorCode(TsyncErrc::kInvalidArgument));
  EXPECT_EQ(tb->GetStatus().Bits(), 0u); // rejected updates leave the time base untouched
  TimeBaseConfig c;
  c.timeout = Duration::zero();
  EXPECT_FALSE(TimeBaseResource::Create(c).HasValue());
}

TEST(TsyncDomain, MessagesAndName) {
  EXPECT_STREQ(GetTsyncDomain().Name(), "Tsync");
  EXPECT_STREQ(MakeErrorCode(TsyncErrc::kNotFound).Message(), "time base not found");
  EXPECT_STREQ(MakeErrorCode(TsyncErrc::kNotSynchronized).Message(), "time base not synchronized");
}

TEST(TsyncDefaultClock, WorksWithRealClock) {
  auto tb = TimeBaseResource::Create({}).Value();
  TimeBaseProvider p(tb);
  ASSERT_TRUE(p.Update(T(milliseconds(42))).HasValue());
  EXPECT_GE(Ms(tb->GetCurrentTime().Value().time), 42);
  EXPECT_TRUE(tb->GetStatus().Test(StatusFlag::kSynchronized));
}

} // namespace
