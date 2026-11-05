#include <gtest/gtest.h>

#include "ara/nm/nm_error_domain.h"
#include "ara/nm/nm_pdu.h"

using namespace ara::nm;

TEST(NmPdu, RoundTrip) {
  NmPdu p;
  p.sourceNodeId = 0x42;
  p.repeatMessageRequest = true;
  p.partialNetworkInfo = true;
  p.pnRequest = 0x80000005U;
  p.userData = {0xAA, 0x55};
  auto b = p.Encode();
  EXPECT_EQ(b[0], 0x42);
  EXPECT_EQ(b[1], 0x11);
  EXPECT_EQ(b[2], 0x05);
  EXPECT_EQ(b[5], 0x80);
  auto d = NmPdu::Decode(b.data(), b.size());
  ASSERT_TRUE(d.HasValue());
  EXPECT_EQ(d.Value(), p);
}

TEST(NmPdu, WrongLengthRejected) {
  std::uint8_t raw[7] = {};
  auto d = NmPdu::Decode(raw, 7);
  ASSERT_FALSE(d.HasValue());
  EXPECT_EQ(d.Error(), MakeErrorCode(NmErrc::kBadPdu));
}

TEST(NmPdu, BitmapWithoutPniRejected) {
  std::uint8_t raw[8] = {1, 0, 1, 0, 0, 0, 0, 0};
  EXPECT_FALSE(NmPdu::Decode(raw, 8).HasValue());
}

TEST(NmPdu, ReservedBitsIgnored) {
  std::uint8_t raw[8] = {7, 0xFE, 0, 0, 0, 0, 1, 2};
  auto d = NmPdu::Decode(raw, 8);
  ASSERT_TRUE(d.HasValue());
  EXPECT_FALSE(d.Value().repeatMessageRequest);
  EXPECT_TRUE(d.Value().partialNetworkInfo);
  EXPECT_EQ(d.Value().userData[1], 2);
}

TEST(NmErrors, DomainMessages) {
  EXPECT_STREQ(GetNmDomain().Name(), "Nm");
  EXPECT_STREQ(MakeErrorCode(NmErrc::kNotRequested).Message(), "network was not requested");
}
