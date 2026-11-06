// Gap-closing tests: rate flags, precision measurement, time validation, offset time base provider
// (doc/spec-conformance/tsync.md).
#include <gtest/gtest.h>

#include <vector>

#include "ara/tsync/synchronized_time_base.h"
#include "ara/tsync/tsync_error_domain.h"

using namespace ara::tsync;
using namespace std::chrono;
using Clk = steady_clock;

namespace {
Timestamp T(milliseconds ms) { return Timestamp(duration_cast<Duration>(ms)); }

struct Gap : ::testing::Test {
  Clk::time_point now{Clk::time_point{} + hours(1)};
  std::shared_ptr<TimeBaseResource> tb;
  ara::core::InstanceSpecifier port{"Vehicle/GapTime"};

  void TearDown() override { TimeBaseRegistry::Unbind("Vehicle/GapTime"); }
  void Make(TimeBaseConfig c = {}) {
    c.clock = [this] { return now; };
    auto r = TimeBaseResource::Create(std::move(c));
    ASSERT_TRUE(r.HasValue());
    tb = r.Value();
    TimeBaseRegistry::Bind("Vehicle/GapTime", tb);
  }
  void Advance(milliseconds d) { now += d; }
};
} // namespace

// ------------------------------------------------------------------------------ rate flags
TEST_F(Gap, RateCorrectedIsFalseUntilARateIsCalculated) { // SWS_TS_14224
  Make();
  TimeBaseProvider prov(tb);
  ASSERT_TRUE(prov.Update(T(milliseconds(1000))).HasValue());
  SynchronizedTimeBaseConsumer c(port);
  EXPECT_FALSE(c.GetTimeWithStatus().GetRateCorrected());
  Advance(milliseconds(100));
  ASSERT_TRUE(prov.Update(T(milliseconds(1100)), 2e-6).HasValue());
  EXPECT_TRUE(c.GetTimeWithStatus().GetRateCorrected());
  Advance(milliseconds(100));
  ASSERT_TRUE(prov.Update(T(milliseconds(1200)), 0.0).HasValue());
  EXPECT_TRUE(c.GetTimeWithStatus().GetRateCorrected()); // sticky
}

TEST_F(Gap, RateExceededUsesTheThresholdAndZeroDisablesIt) { // SWS_TS_14228
  TimeBaseConfig cfg;
  cfg.rateCorrectionThreshold = 1e-4;
  Make(cfg);
  TimeBaseProvider prov(tb);
  SynchronizedTimeBaseConsumer c(port);
  ASSERT_TRUE(prov.Update(T(milliseconds(0)), 5e-5).HasValue());
  EXPECT_FALSE(c.GetTimeWithStatus().GetRateExceeded());
  ASSERT_TRUE(prov.Update(T(milliseconds(0)), -2e-4).HasValue()); // |rate| > threshold
  EXPECT_TRUE(c.GetTimeWithStatus().GetRateExceeded());
  ASSERT_TRUE(prov.Update(T(milliseconds(0)), 1e-4).HasValue()); // equal is not exceeded
  EXPECT_FALSE(c.GetTimeWithStatus().GetRateExceeded());
  Make(); // threshold 0 -> check disabled
  TimeBaseProvider prov2(tb);
  ASSERT_TRUE(prov2.Update(T(milliseconds(0)), 0.5).HasValue());
  EXPECT_FALSE(SynchronizedTimeBaseConsumer(port).GetTimeWithStatus().GetRateExceeded());
}

TEST_F(Gap, ProviderRateCorrectionSetsRateCorrected) {
  TimeBaseConfig cfg;
  cfg.allowProviderRateCorrection = true;
  cfg.providerRateDeviationMax = 1e-3;
  Make(cfg);
  SynchronizedTimeBaseProvider p(port);
  ASSERT_TRUE(p.SetTime(T(milliseconds(10))).HasValue());
  SynchronizedTimeBaseConsumer c(port);
  EXPECT_FALSE(c.GetTimeWithStatus().GetRateCorrected());
  ASSERT_TRUE(p.SetRateCorrection(1e-4).HasValue());
  EXPECT_TRUE(c.GetTimeWithStatus().GetRateCorrected());
}

// ------------------------------------------------------------------ precision measurement
TEST_F(Gap, PrecisionMeasurementRecordsAllFieldsForASynchronizedBase) { // SWS_TS_00800
  Make();
  TimeBaseProvider prov(tb);
  SynchronizedTimeBaseConsumer c(port);
  std::vector<TimePrecisionMeasurement> got;
  ASSERT_TRUE(c.RegisterTimePrecisionMeasurementNotifier([&](TimePrecisionMeasurement const &m) { got.push_back(m); })
                  .HasValue());
  ASSERT_TRUE(prov.Update(T(milliseconds(2500)), 3e-6).HasValue());
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(microseconds(150)).HasValue());
  ASSERT_EQ(got.size(), 1U);
  EXPECT_EQ(got[0].glbSeconds, 2U);
  EXPECT_EQ(got[0].glbNanoSeconds, 500000000U);
  EXPECT_NE(got[0].timeBaseStatus & static_cast<std::uint32_t>(StatusFlag::kSynchronized), 0U);
  EXPECT_DOUBLE_EQ(got[0].rateDeviation, 3e-6);
  EXPECT_EQ(got[0].pathDelay, 150000U);
  auto locNs = duration_cast<nanoseconds>(now.time_since_epoch()).count();
  EXPECT_EQ(got[0].locSeconds, static_cast<std::uint64_t>(locNs / 1000000000LL));
  EXPECT_EQ(got[0].virtualLocalTimeLow, static_cast<std::uint32_t>(static_cast<std::uint64_t>(locNs) & 0xFFFFFFFFULL));
}

TEST_F(Gap, PrecisionMeasurementForOffsetBaseOnlyHasGlobalTimeAndStatus) { // SWS_TS_00801
  TimeBaseConfig cfg;
  cfg.isOffsetTimeBase = true;
  Make(cfg);
  OffsetTimeBaseProvider o(port);
  ASSERT_TRUE(o.SetOffsetTime(T(milliseconds(7000))).HasValue());
  SynchronizedTimeBaseConsumer c(port);
  TimePrecisionMeasurement m;
  ASSERT_TRUE(c.RegisterTimePrecisionMeasurementNotifier([&](TimePrecisionMeasurement const &x) { m = x; }).HasValue());
  TimeBaseProvider prov(tb);
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(microseconds(9)).HasValue());
  EXPECT_EQ(m.glbSeconds, 7U);
  EXPECT_NE(m.timeBaseStatus, 0U);
  EXPECT_EQ(m.pathDelay, 0U);
  EXPECT_EQ(m.locSeconds, 0U);
  EXPECT_EQ(m.virtualLocalTimeLow, 0U);
}

TEST_F(Gap, PrecisionNotifierRejectedForGlobalTimeMasterAndEdgeCases) { // SWS_TS_00803
  TimeBaseConfig cfg;
  cfg.isSystemWideGlobalTimeMaster = true;
  Make(cfg);
  SynchronizedTimeBaseConsumer c(port);
  EXPECT_EQ(c.RegisterTimePrecisionMeasurementNotifier([](TimePrecisionMeasurement const &) {}).Error(),
            MakeErrorCode(TsyncErrc::kFunctionNotSupported));
  Make();
  EXPECT_EQ(c.RegisterTimePrecisionMeasurementNotifier(nullptr).Error(), MakeErrorCode(TsyncErrc::kInvalidArgument));
  TimeBaseProvider prov(tb);
  EXPECT_EQ(prov.ReportPrecisionMeasurement(microseconds(1)).Error(), MakeErrorCode(TsyncErrc::kNotSynchronized));
  TimeBaseRegistry::Unbind("Vehicle/GapTime");
  EXPECT_EQ(c.RegisterTimePrecisionMeasurementNotifier([](TimePrecisionMeasurement const &) {}).Error(),
            MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
}

TEST_F(Gap, PrecisionNotifierCanBeUnregisteredAndOverwritten) {
  Make();
  TimeBaseProvider prov(tb);
  ASSERT_TRUE(prov.Update(T(milliseconds(1))).HasValue());
  SynchronizedTimeBaseConsumer c(port);
  int a = 0, b = 0;
  ASSERT_TRUE(c.RegisterTimePrecisionMeasurementNotifier([&](TimePrecisionMeasurement const &) { ++a; }).HasValue());
  ASSERT_TRUE(c.RegisterTimePrecisionMeasurementNotifier([&](TimePrecisionMeasurement const &) { ++b; }).HasValue());
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(microseconds(1)).HasValue());
  EXPECT_EQ(a, 0);
  EXPECT_EQ(b, 1);
  c.UnregisterTimePrecisionMeasurementNotifier();
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(microseconds(1)).HasValue());
  EXPECT_EQ(b, 1);
}

TEST_F(Gap, PathDelayIsClampedToTheFieldRange) {
  Make();
  TimeBaseProvider prov(tb);
  ASSERT_TRUE(prov.Update(T(milliseconds(1))).HasValue());
  SynchronizedTimeBaseConsumer c(port);
  std::uint32_t pd = 7;
  ASSERT_TRUE(c.RegisterTimePrecisionMeasurementNotifier([&](TimePrecisionMeasurement const &m) { pd = m.pathDelay; }).HasValue());
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(seconds(10)).HasValue());
  EXPECT_EQ(pd, 0xFFFFFFFFU);
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(nanoseconds(-5)).HasValue());
  EXPECT_EQ(pd, 0U);
}

// ---------------------------------------------------------------------------- time validation
TEST_F(Gap, ValidationDataReachesConsumerAndProviderNotifiers) { // SWS_TS_00424..00427, 01016, 01114
  Make();
  SynchronizedTimeBaseConsumer c(port);
  SynchronizedTimeBaseProvider p(port);
  std::vector<TimeValidationData> viaConsumer, viaProvider;
  ASSERT_TRUE(c.RegisterTimeValidationNotification([&](TimeValidationData const &d) { viaConsumer.push_back(d); }).HasValue());
  ASSERT_TRUE(p.RegisterTimeValidationNotification([&](TimeValidationData const &d) { viaProvider.push_back(d); }).HasValue());
  TimeValidationData d;
  d.sequenceId = 42;
  d.pathDelayNs = 1234;
  ASSERT_TRUE(p.SetMasterTimingData(d).HasValue());
  ASSERT_TRUE(p.SetPdelayResponderData(d).HasValue());
  ASSERT_EQ(viaConsumer.size(), 2U);
  EXPECT_EQ(viaConsumer[0].kind, TimeValidationData::Kind::kMasterTimingData);
  EXPECT_EQ(viaConsumer[1].kind, TimeValidationData::Kind::kPdelayResponderData);
  EXPECT_EQ(viaConsumer[0].sequenceId, 42);
  EXPECT_EQ(viaProvider.size(), 2U);
}

TEST_F(Gap, ProviderValidationNotifierOverwritesAndUnregisters) { // SWS_TS_01114/01115
  Make();
  SynchronizedTimeBaseProvider p(port);
  int a = 0, b = 0;
  ASSERT_TRUE(p.RegisterTimeValidationNotification([&](TimeValidationData const &) { ++a; }).HasValue());
  ASSERT_TRUE(p.RegisterTimeValidationNotification([&](TimeValidationData const &) { ++b; }).HasValue());
  ASSERT_TRUE(p.SetMasterTimingData({}).HasValue());
  EXPECT_EQ(a, 0);
  EXPECT_EQ(b, 1);
  p.UnregisterTimeValidationNotification();
  ASSERT_TRUE(p.SetMasterTimingData({}).HasValue());
  EXPECT_EQ(b, 1);
  EXPECT_EQ(p.RegisterTimeValidationNotification(nullptr).Error(), MakeErrorCode(TsyncErrc::kInvalidArgument));
}

TEST_F(Gap, ValidationOnUnboundBaseIsDaemonLost) {
  Make();
  TimeBaseRegistry::Unbind("Vehicle/GapTime");
  SynchronizedTimeBaseConsumer c(port);
  SynchronizedTimeBaseProvider p(port);
  EXPECT_EQ(c.RegisterTimeValidationNotification([](TimeValidationData const &) {}).Error(),
            MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  EXPECT_EQ(p.RegisterTimeValidationNotification([](TimeValidationData const &) {}).Error(),
            MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  EXPECT_EQ(p.SetMasterTimingData({}).Error(), MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
}

TEST_F(Gap, DestroyedConsumerStopsReceivingValidationAndPrecision) {
  Make();
  TimeBaseProvider prov(tb);
  ASSERT_TRUE(prov.Update(T(milliseconds(1))).HasValue());
  int n = 0;
  {
    SynchronizedTimeBaseConsumer c(port);
    ASSERT_TRUE(c.RegisterTimeValidationNotification([&](TimeValidationData const &) { ++n; }).HasValue());
    ASSERT_TRUE(c.RegisterTimePrecisionMeasurementNotifier([&](TimePrecisionMeasurement const &) { ++n; }).HasValue());
    prov.PublishValidationData({});
    ASSERT_TRUE(prov.ReportPrecisionMeasurement(microseconds(1)).HasValue());
    EXPECT_EQ(n, 2);
  }
  prov.PublishValidationData({});
  ASSERT_TRUE(prov.ReportPrecisionMeasurement(microseconds(1)).HasValue());
  EXPECT_EQ(n, 2);
}

// ------------------------------------------------------------------------ offset time base
TEST_F(Gap, SetOffsetTimeStepsAnOffsetTimeBase) { // SWS_TS_01207
  TimeBaseConfig cfg;
  cfg.isOffsetTimeBase = true;
  Make(cfg);
  OffsetTimeBaseProvider o(port);
  std::uint8_t ud[2] = {0xAB, 0xCD};
  ASSERT_TRUE(o.SetOffsetTime(T(milliseconds(5000)), ud, 2).HasValue());
  EXPECT_EQ(duration_cast<milliseconds>(o.GetCurrentTime().time_since_epoch()).count(), 5000);
  EXPECT_EQ(o.GetUserData(), (std::vector<std::uint8_t>{0xAB, 0xCD}));
  Advance(milliseconds(250));
  EXPECT_EQ(duration_cast<milliseconds>(o.GetCurrentTime().time_since_epoch()).count(), 5250);
  // an unconditional step: even a small change does not slew
  ASSERT_TRUE(o.SetOffsetTime(T(milliseconds(5255))).HasValue());
  EXPECT_EQ(duration_cast<milliseconds>(o.GetCurrentTime().time_since_epoch()).count(), 5255);
  SynchronizedTimeBaseConsumer c(port);
  EXPECT_EQ(c.GetTimeWithStatus().GetSynchronizationStatus(), SynchronizationStatus::kSynchronized);
}

TEST_F(Gap, SetOffsetTimeErrors) {
  Make(); // not an offset time base
  OffsetTimeBaseProvider o(port);
  EXPECT_EQ(o.SetOffsetTime(T(milliseconds(1))).Error(), MakeErrorCode(TsyncErrc::kTimeCannotSet));
  TimeBaseConfig cfg;
  cfg.isOffsetTimeBase = true;
  Make(cfg);
  std::uint8_t big[4] = {1, 2, 3, 4};
  EXPECT_EQ(o.SetOffsetTime(T(milliseconds(1)), big, 4).Error(), MakeErrorCode(TsyncErrc::kInvalidArgument));
  EXPECT_EQ(o.SetUserData(big, 4).Error(), MakeErrorCode(TsyncErrc::kInvalidArgument));
  TimeBaseRegistry::Unbind("Vehicle/GapTime");
  EXPECT_EQ(o.SetOffsetTime(T(milliseconds(1))).Error(), MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  EXPECT_EQ(o.SetRateCorrection(0.0).Error(), MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  EXPECT_EQ(o.SetUserData(nullptr, 0).Error(), MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  EXPECT_EQ(o.GetRateDeviation(), 0.0);
  EXPECT_TRUE(o.GetUserData().empty());
  EXPECT_EQ(o.GetCurrentTime().time_since_epoch().count(), 0);
}

TEST_F(Gap, OffsetProviderRateCorrectionAndValidation) { // SWS_TS_01209, 01213, 01214
  TimeBaseConfig cfg;
  cfg.isOffsetTimeBase = true;
  cfg.allowProviderRateCorrection = true;
  cfg.providerRateDeviationMax = 1e-3;
  Make(cfg);
  OffsetTimeBaseProvider o(port);
  ASSERT_TRUE(o.SetOffsetTime(T(milliseconds(0))).HasValue());
  ASSERT_TRUE(o.SetRateCorrection(1e-4).HasValue());
  EXPECT_DOUBLE_EQ(o.GetRateDeviation(), 1e-4);
  EXPECT_EQ(o.SetRateCorrection(0.5).Error(), MakeErrorCode(TsyncErrc::kLimitsExceeded));
  std::vector<TimeValidationData::Kind> kinds;
  ASSERT_TRUE(o.RegisterTimeValidationNotification([&](TimeValidationData const &d) { kinds.push_back(d.kind); })
                  .HasValue());
  ASSERT_TRUE(o.SetPdelayResponderData({}).HasValue());
  ASSERT_TRUE(o.SetMasterTimingData({}).HasValue());
  ASSERT_EQ(kinds.size(), 2U);
  EXPECT_EQ(kinds[0], TimeValidationData::Kind::kPdelayResponderData);
  EXPECT_EQ(kinds[1], TimeValidationData::Kind::kMasterTimingData);
  o.UnregisterTimeValidationNotification();
  ASSERT_TRUE(o.SetMasterTimingData({}).HasValue());
  EXPECT_EQ(kinds.size(), 2U);
}

TEST(TsyncGapErrors, TimeCannotSetCode) {
  auto ec = MakeErrorCode(TsyncErrc::kTimeCannotSet);
  EXPECT_STREQ(ec.Domain().Name(), "Tsync");
  EXPECT_STREQ(ec.Message(), "time cannot be set");
}
