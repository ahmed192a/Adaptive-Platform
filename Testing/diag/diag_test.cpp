#include <gtest/gtest.h>

#include "ara/diag/diagnostic_server.h"

using namespace ara::diag;
using uds::Nrc;
using uds::Session;
using Clk = std::chrono::steady_clock;
using ara::core::Result;

namespace {

struct Fixture : ::testing::Test {
  Clk::time_point now{Clk::time_point{} + std::chrono::hours(1)};
  std::uint8_t stored[2]{0x12, 0x34};
  DiagnosticServer server{[this] {
    DiagnosticServer::Config c;
    c.clock = [this] { return now; };
    return c;
  }()};

  void SetUp() override {
    DataIdentifier did;
    did.read = [this]() -> Result<ByteVector, Nrc> { return ByteVector{stored[0], stored[1]}; };
    did.write = [this](ByteVector const &d) -> Result<void, Nrc> {
      if (d[0] == 0xFF) return Result<void, Nrc>::FromError(Nrc::kRequestOutOfRange);
      stored[0] = d[0];
      stored[1] = d[1];
      return {};
    };
    did.writeLength = 2;
    ASSERT_TRUE(server.RegisterDataIdentifier(0xF190, did).HasValue());
    DataIdentifier ro;
    ro.read = []() -> Result<ByteVector, Nrc> { return ByteVector{0xAA}; };
    ASSERT_TRUE(server.RegisterDataIdentifier(0xF191, ro).HasValue());
  }
  ByteVector Req(ByteVector r) {
    auto x = server.Dispatch(r);
    EXPECT_TRUE(x.has_value());
    return x.value_or(ByteVector{});
  }
};

TEST_F(Fixture, UnknownServiceAndEmptyRequest) {
  EXPECT_EQ(Req({0x99}), (ByteVector{0x7F, 0x99, 0x11}));
  EXPECT_FALSE(server.Dispatch({}).has_value());
}

TEST_F(Fixture, SessionControlAnswersWithTimingParameters) {
  EXPECT_EQ(Req({0x10, 0x03}), (ByteVector{0x50, 0x03, 0x00, 0x32, 0x01, 0xF4}));
  EXPECT_EQ(server.CurrentSession(), Session::kExtended);
  EXPECT_EQ(Req({0x10, 0x09}), (ByteVector{0x7F, 0x10, 0x12}));
  EXPECT_EQ(Req({0x10}), (ByteVector{0x7F, 0x10, 0x13}));
  EXPECT_EQ(Req({0x10, 0x01, 0x00}), (ByteVector{0x7F, 0x10, 0x13}));
}

TEST_F(Fixture, SuppressPositiveResponseBit) {
  EXPECT_FALSE(server.Dispatch({0x10, 0x83}).has_value());
  EXPECT_EQ(server.CurrentSession(), Session::kExtended);
  EXPECT_FALSE(server.Dispatch({0x3E, 0x80}).has_value());
  // errors are never suppressed
  EXPECT_EQ(Req({0x10, 0x89}), (ByteVector{0x7F, 0x10, 0x12}));
}

TEST_F(Fixture, S3TimeoutFallsBackToDefaultSession) {
  Req({0x10, 0x03});
  now += std::chrono::milliseconds(4900);
  EXPECT_EQ(Req({0x3E, 0x00}), (ByteVector{0x7E, 0x00})); // tester present keeps it alive
  now += std::chrono::milliseconds(4900);
  EXPECT_EQ(server.CurrentSession(), Session::kExtended);
  now += std::chrono::milliseconds(200);
  EXPECT_EQ(server.CurrentSession(), Session::kDefault);
}

TEST_F(Fixture, TesterPresentValidation) {
  EXPECT_EQ(Req({0x3E, 0x01}), (ByteVector{0x7F, 0x3E, 0x12}));
  EXPECT_EQ(Req({0x3E}), (ByteVector{0x7F, 0x3E, 0x13}));
}

TEST_F(Fixture, ReadDataByIdentifierSingleAndMultiple) {
  EXPECT_EQ(Req({0x22, 0xF1, 0x90}), (ByteVector{0x62, 0xF1, 0x90, 0x12, 0x34}));
  EXPECT_EQ(Req({0x22, 0xF1, 0x90, 0xF1, 0x91}),
            (ByteVector{0x62, 0xF1, 0x90, 0x12, 0x34, 0xF1, 0x91, 0xAA}));
  EXPECT_EQ(Req({0x22, 0xF1, 0x99}), (ByteVector{0x7F, 0x22, 0x31}));
  EXPECT_EQ(Req({0x22, 0xF1}), (ByteVector{0x7F, 0x22, 0x13}));
  EXPECT_EQ(Req({0x22}), (ByteVector{0x7F, 0x22, 0x13}));
}

TEST_F(Fixture, WriteRequiresExtendedSessionAndLength) {
  EXPECT_EQ(Req({0x2E, 0xF1, 0x90, 0x01, 0x02}), (ByteVector{0x7F, 0x2E, 0x31})); // default session
  Req({0x10, 0x03});
  EXPECT_EQ(Req({0x2E, 0xF1, 0x90, 0x01}), (ByteVector{0x7F, 0x2E, 0x13}));
  EXPECT_EQ(Req({0x2E, 0xF1, 0x90, 0x0A, 0x0B}), (ByteVector{0x6E, 0xF1, 0x90}));
  EXPECT_EQ(Req({0x22, 0xF1, 0x90}), (ByteVector{0x62, 0xF1, 0x90, 0x0A, 0x0B}));
  EXPECT_EQ(Req({0x2E, 0xF1, 0x90, 0xFF, 0x00}), (ByteVector{0x7F, 0x2E, 0x31})); // handler NRC
  EXPECT_EQ(Req({0x2E, 0xF1, 0x91, 0x01}), (ByteVector{0x7F, 0x2E, 0x31}));        // read-only DID
  EXPECT_EQ(Req({0x2E, 0xF1, 0x90}), (ByteVector{0x7F, 0x2E, 0x13}));
}

TEST_F(Fixture, EcuReset) {
  EXPECT_EQ(Req({0x11, 0x01}), (ByteVector{0x7F, 0x11, 0x11})); // no handler
  std::uint8_t seen = 0;
  bool ok = true;
  server.SetResetHandler([&](std::uint8_t t) { seen = t; return ok; });
  EXPECT_EQ(Req({0x11, 0x03}), (ByteVector{0x51, 0x03}));
  EXPECT_EQ(seen, 3);
  ok = false;
  EXPECT_EQ(Req({0x11, 0x01}), (ByteVector{0x7F, 0x11, 0x22}));
  EXPECT_EQ(Req({0x11, 0x42}), (ByteVector{0x7F, 0x11, 0x12}));
}

TEST_F(Fixture, CustomServiceHonoursSessionsAndNrc) {
  auto r = server.RegisterService(0x31, uds::SessionBit(Session::kExtended),
                                  [](ByteVector const &d) -> ServiceResult {
                                    if (d.empty()) return ServiceResult::FromError(Nrc::kIncorrectMessageLengthOrInvalidFormat);
                                    return ByteVector{d[0], 0x55};
                                  });
  ASSERT_TRUE(r.HasValue());
  EXPECT_EQ(Req({0x31, 0x01}), (ByteVector{0x7F, 0x31, 0x7F}));
  Req({0x10, 0x03});
  EXPECT_EQ(Req({0x31, 0x01}), (ByteVector{0x71, 0x01, 0x55}));
  EXPECT_EQ(Req({0x31}), (ByteVector{0x7F, 0x31, 0x13}));
}

TEST_F(Fixture, ThrowingHandlerBecomesGeneralReject) {
  ASSERT_TRUE(server.RegisterService(0x85, kAllSessions, [](ByteVector const &) -> ServiceResult {
                      throw std::runtime_error("boom");
                    }).HasValue());
  EXPECT_EQ(Req({0x85}), (ByteVector{0x7F, 0x85, 0x10}));
}

TEST_F(Fixture, RegistrationErrors) {
  auto h = [](ByteVector const &) -> ServiceResult { return ByteVector{}; };
  EXPECT_EQ(server.RegisterService(0x22, kAllSessions, h).Error(), MakeErrorCode(DiagErrc::kAlreadyOffered));
  EXPECT_EQ(server.RegisterService(0x31, 0, h).Error(), MakeErrorCode(DiagErrc::kInvalidArgument));
  EXPECT_EQ(server.RegisterService(0x7F, kAllSessions, h).Error(), MakeErrorCode(DiagErrc::kInvalidArgument));
  EXPECT_EQ(server.RegisterService(0xC0, kAllSessions, h).Error(), MakeErrorCode(DiagErrc::kInvalidArgument));
  EXPECT_EQ(server.RegisterService(0x50, kAllSessions, h).Error(), MakeErrorCode(DiagErrc::kInvalidArgument));
  ASSERT_TRUE(server.RegisterService(0x31, kAllSessions, h).HasValue());
  EXPECT_EQ(server.RegisterService(0x31, kAllSessions, h).Error(), MakeErrorCode(DiagErrc::kAlreadyOffered));
  DataIdentifier d;
  EXPECT_EQ(server.RegisterDataIdentifier(1, d).Error(), MakeErrorCode(DiagErrc::kInvalidArgument));
  d.read = []() -> Result<ByteVector, Nrc> { return ByteVector{}; };
  ASSERT_TRUE(server.RegisterDataIdentifier(1, d).HasValue());
  EXPECT_EQ(server.RegisterDataIdentifier(1, d).Error(), MakeErrorCode(DiagErrc::kAlreadyOffered));
}

TEST(DiagDomain, NameAndThrow) {
  auto ec = MakeErrorCode(DiagErrc::kRejected);
  EXPECT_STREQ(ec.Domain().Name(), "Diag");
  EXPECT_THROW(ec.ThrowAsException(), DiagException);
}

} // namespace
