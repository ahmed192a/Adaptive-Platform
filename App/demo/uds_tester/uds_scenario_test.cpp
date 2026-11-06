#include <gtest/gtest.h>

#include <sstream>

#include "uds_scenario.h"

using namespace ap::demo;

TEST(UdsScenario, ScriptedSessionPasses) {
  DemoEcu ecu;
  std::ostringstream out;
  auto r = RunScenario(ecu, &out);
  EXPECT_EQ(r.failed, 0) << out.str();
  EXPECT_EQ(r.passed, 35);
  EXPECT_EQ(ecu.Name(), "Roadster");
  EXPECT_EQ(ecu.ResetCount(), 1);
  EXPECT_EQ(ecu.FlashImage().size(), 12U);
  EXPECT_NE(out.str().find("negative response: invalidKey (0x35) for SecurityAccess"), std::string::npos);
  EXPECT_NE(out.str().find("checks passed, 0 failed"), std::string::npos);
}

TEST(UdsScenario, ScenarioCanRunWithoutPrinting) {
  DemoEcu ecu;
  auto r = RunScenario(ecu, nullptr);
  EXPECT_EQ(r.failed, 0);
  EXPECT_EQ(r.trace.size(), static_cast<std::size_t>(r.passed));
}

TEST(UdsScenario, HexParsingAndDescriptions) {
  EXPECT_EQ(HexString({0x22, 0xF1, 0x90}), "22 F1 90");
  EXPECT_EQ(*ParseHex("22 f1,90"), (ara::diag::ByteVector{0x22, 0xF1, 0x90}));
  EXPECT_EQ(*ParseHex("2ef190"), (ara::diag::ByteVector{0x2E, 0xF1, 0x90}));
  EXPECT_FALSE(ParseHex("").has_value());
  EXPECT_FALSE(ParseHex("2").has_value());
  EXPECT_FALSE(ParseHex("2G").has_value());
  EXPECT_EQ(Describe({0x22, 0xF1, 0x90}), "ReadDataByIdentifier (0x22) request");
  EXPECT_EQ(Describe({0x62, 0xF1, 0x90}), "positive response: ReadDataByIdentifier");
  EXPECT_EQ(Describe({0x7F, 0x27, 0x35}), "negative response: invalidKey (0x35) for SecurityAccess");
  EXPECT_EQ(Describe({}), "empty");
}

TEST(UdsScenario, S3TimeoutDropsTheExtendedSession) {
  DemoEcu ecu;
  ecu.Server().Dispatch({0x10, 0x03});
  EXPECT_EQ(ecu.Server().CurrentSession(), ara::diag::uds::Session::kExtended);
  ecu.Advance(std::chrono::seconds(6)); // S3 = 5 s
  EXPECT_EQ(ecu.Server().CurrentSession(), ara::diag::uds::Session::kDefault);
}
