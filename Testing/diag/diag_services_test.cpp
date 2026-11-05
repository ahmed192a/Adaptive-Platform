#include <gtest/gtest.h>

#include "ara/diag/diagnostic_server.h"

using namespace ara::diag;
using uds::Nrc;
using Clk = std::chrono::steady_clock;
using ara::core::Result;

namespace {

struct Svc : ::testing::Test {
  Clk::time_point now{Clk::time_point{} + std::chrono::hours(1)};
  int routineRuns{0};
  DiagnosticServer server{[this] {
    DiagnosticServer::Config c;
    c.clock = [this] { return now; };
    return c;
  }()};

  void SetUp() override {
    SecurityProvider sp;
    sp.seed = [](std::uint8_t) -> Result<ByteVector, Nrc> { return ByteVector{0x01, 0x02}; };
    // key = seed bytes each XOR 0xFF
    sp.validateKey = [](std::uint8_t, ByteVector const &seed, ByteVector const &key) {
      return key.size() == seed.size() && key[0] == (seed[0] ^ 0xFF) && key[1] == (seed[1] ^ 0xFF);
    };
    sp.maxAttempts = 2;
    sp.lockout = std::chrono::seconds(10);
    server.SetSecurityProvider(sp);

    Routine r;
    r.start = [this](ByteVector const &o) -> Result<ByteVector, Nrc> {
      ++routineRuns;
      return o;
    };
    r.requestResults = [](ByteVector const &) -> Result<ByteVector, Nrc> { return ByteVector{0x42}; };
    r.securityLevel = 1;
    ASSERT_TRUE(server.RegisterRoutine(0x0203, r).HasValue());

    DataIdentifier d;
    d.read = []() -> Result<ByteVector, Nrc> { return ByteVector{0x01}; };
    d.write = [](ByteVector const &) -> Result<void, Nrc> { return {}; };
    d.writeSessions = kAllSessions;
    d.writeSecurityLevel = 1;
    ASSERT_TRUE(server.RegisterDataIdentifier(0xF1A5, d).HasValue());
  }
  ByteVector Req(ByteVector r) {
    auto x = server.Dispatch(r);
    EXPECT_TRUE(x.has_value());
    return x.value_or(ByteVector{});
  }
  void Unlock() {
    EXPECT_EQ(Req({0x27, 0x01}), (ByteVector{0x67, 0x01, 0x01, 0x02}));
    EXPECT_EQ(Req({0x27, 0x02, 0xFE, 0xFD}), (ByteVector{0x67, 0x02}));
  }
};

TEST_F(Svc, SecurityAccessUnlocksAndSessionChangeRelocks) {
  EXPECT_EQ(server.CurrentSecurityLevel(), 0);
  Unlock();
  EXPECT_EQ(server.CurrentSecurityLevel(), 1);
  EXPECT_EQ(Req({0x27, 0x01}), (ByteVector{0x67, 0x01, 0x00, 0x00})); // already unlocked: zero seed
  Req({0x10, 0x03});
  EXPECT_EQ(server.CurrentSecurityLevel(), 0);
}

TEST_F(Svc, SecurityAccessSequenceAndLengthChecks) {
  EXPECT_EQ(Req({0x27, 0x02, 0x00, 0x00}), uds::NegativeResponse(0x27, Nrc::kRequestSequenceError));
  EXPECT_EQ(Req({0x27}), uds::NegativeResponse(0x27, Nrc::kIncorrectMessageLengthOrInvalidFormat));
  EXPECT_EQ(Req({0x27, 0x00}), uds::NegativeResponse(0x27, Nrc::kSubFunctionNotSupported));
  Req({0x27, 0x01});
  EXPECT_EQ(Req({0x27, 0x02}), uds::NegativeResponse(0x27, Nrc::kIncorrectMessageLengthOrInvalidFormat));
}

TEST_F(Svc, SecurityAccessWrongKeyLocksOutThenRecovers) {
  Req({0x27, 0x01});
  EXPECT_EQ(Req({0x27, 0x02, 0, 0}), uds::NegativeResponse(0x27, Nrc::kInvalidKey));
  Req({0x27, 0x01});
  EXPECT_EQ(Req({0x27, 0x02, 0, 0}), uds::NegativeResponse(0x27, Nrc::kExceededNumberOfAttempts));
  EXPECT_EQ(Req({0x27, 0x01}), uds::NegativeResponse(0x27, Nrc::kRequiredTimeDelayNotExpired));
  now += std::chrono::seconds(11);
  Unlock();
  EXPECT_EQ(server.CurrentSecurityLevel(), 1);
}

TEST_F(Svc, SecurityAccessWithoutProviderIsNotSupported) {
  DiagnosticServer bare;
  EXPECT_EQ(*bare.Dispatch({0x27, 0x01}), uds::NegativeResponse(0x27, Nrc::kServiceNotSupported));
}

TEST_F(Svc, RoutineControlStartRequiresSecurity) {
  EXPECT_EQ(Req({0x31, 0x01, 0x02, 0x03, 0xAA}), uds::NegativeResponse(0x31, Nrc::kSecurityAccessDenied));
  EXPECT_EQ(routineRuns, 0);
  Unlock();
  EXPECT_EQ(Req({0x31, 0x01, 0x02, 0x03, 0xAA}), (ByteVector{0x71, 0x01, 0x02, 0x03, 0xAA}));
  EXPECT_EQ(routineRuns, 1);
  EXPECT_EQ(Req({0x31, 0x03, 0x02, 0x03}), (ByteVector{0x71, 0x03, 0x02, 0x03, 0x42}));
}

TEST_F(Svc, RoutineControlErrors) {
  Unlock();
  EXPECT_EQ(Req({0x31, 0x02, 0x02, 0x03}), uds::NegativeResponse(0x31, Nrc::kSubFunctionNotSupported)); // no stop
  EXPECT_EQ(Req({0x31, 0x04, 0x02, 0x03}), uds::NegativeResponse(0x31, Nrc::kSubFunctionNotSupported));
  EXPECT_EQ(Req({0x31, 0x01, 0x99, 0x99}), uds::NegativeResponse(0x31, Nrc::kRequestOutOfRange));
  EXPECT_EQ(Req({0x31, 0x01}), uds::NegativeResponse(0x31, Nrc::kIncorrectMessageLengthOrInvalidFormat));
  EXPECT_FALSE(server.Dispatch({0x31, 0x81, 0x02, 0x03}).has_value()); // suppressPositiveResponse
  Routine none;
  EXPECT_FALSE(server.RegisterRoutine(0x1, none).HasValue());
  Routine again;
  again.start = [](ByteVector const &) -> Result<ByteVector, Nrc> { return ByteVector{}; };
  EXPECT_FALSE(server.RegisterRoutine(0x0203, again).HasValue());
}

TEST_F(Svc, WriteDidHonoursSecurityLevel) {
  EXPECT_EQ(Req({0x2E, 0xF1, 0xA5, 0x01}), uds::NegativeResponse(0x2E, Nrc::kSecurityAccessDenied));
  Unlock();
  EXPECT_EQ(Req({0x2E, 0xF1, 0xA5, 0x01}), (ByteVector{0x6E, 0xF1, 0xA5}));
}

TEST_F(Svc, DtcMemoryReportAndRead) {
  server.ReportDtc(0x123456, true);
  server.ReportDtc(0xABCDEF, true);
  server.ReportDtc(0xABCDEF, false); // test passes again: testFailed cleared, history bits stay
  EXPECT_EQ(*server.DtcStatus(0x123456), 0x2B);
  EXPECT_EQ(*server.DtcStatus(0xABCDEF), 0x2A);
  EXPECT_FALSE(server.DtcStatus(0x1).has_value());
  EXPECT_EQ(Req({0x19, 0x01, 0x01}), (ByteVector{0x59, 0x01, 0x2B, 0x01, 0x00, 0x01}));
  EXPECT_EQ(Req({0x19, 0x02, 0x08}),
            (ByteVector{0x59, 0x02, 0x2B, 0x12, 0x34, 0x56, 0x2B, 0xAB, 0xCD, 0xEF, 0x2A}));
  EXPECT_EQ(Req({0x19, 0x02, 0x01}), (ByteVector{0x59, 0x02, 0x2B, 0x12, 0x34, 0x56, 0x2B}));
  EXPECT_EQ(Req({0x19, 0x0A}).size(), 3U + 2 * 4);
}

TEST_F(Svc, DtcReadErrors) {
  EXPECT_EQ(Req({0x19}), uds::NegativeResponse(0x19, Nrc::kIncorrectMessageLengthOrInvalidFormat));
  EXPECT_EQ(Req({0x19, 0x01}), uds::NegativeResponse(0x19, Nrc::kIncorrectMessageLengthOrInvalidFormat));
  EXPECT_EQ(Req({0x19, 0x09, 0x00}), uds::NegativeResponse(0x19, Nrc::kSubFunctionNotSupported));
}

TEST_F(Svc, ClearDtc) {
  server.ReportDtc(0x111111, true);
  server.ReportDtc(0x222222, true);
  EXPECT_EQ(Req({0x14, 0x11, 0x11, 0x11}), (ByteVector{0x54}));
  EXPECT_FALSE(server.DtcStatus(0x111111).has_value());
  EXPECT_EQ(Req({0x14, 0x33, 0x33, 0x33}), uds::NegativeResponse(0x14, Nrc::kRequestOutOfRange));
  EXPECT_EQ(Req({0x14, 0xFF, 0xFF, 0xFF}), (ByteVector{0x54}));
  EXPECT_FALSE(server.DtcStatus(0x222222).has_value());
  EXPECT_EQ(Req({0x14, 0xFF}), uds::NegativeResponse(0x14, Nrc::kIncorrectMessageLengthOrInvalidFormat));
}

TEST_F(Svc, ControlDtcSettingFreezesEventMemory) {
  EXPECT_EQ(Req({0x85, 0x02}), (ByteVector{0xC5, 0x02}));
  server.ReportDtc(0x777777, true);
  EXPECT_FALSE(server.DtcStatus(0x777777).has_value());
  EXPECT_EQ(Req({0x85, 0x01}), (ByteVector{0xC5, 0x01}));
  server.ReportDtc(0x777777, true);
  EXPECT_TRUE(server.DtcStatus(0x777777).has_value());
  EXPECT_EQ(Req({0x85, 0x03}), uds::NegativeResponse(0x85, Nrc::kSubFunctionNotSupported));
  EXPECT_FALSE(server.Dispatch({0x85, 0x82}).has_value());
  EXPECT_EQ(Req({0x85}), uds::NegativeResponse(0x85, Nrc::kIncorrectMessageLengthOrInvalidFormat));
}

TEST_F(Svc, BuiltinSidsCannotBeOverridden) {
  auto h = [](ByteVector const &) -> Result<ByteVector, Nrc> { return ByteVector{}; };
  for (std::uint8_t sid : {0x27, 0x31, 0x14, 0x19, 0x85})
    EXPECT_FALSE(server.RegisterService(sid, kAllSessions, h).HasValue());
}

} // namespace
