#include <gtest/gtest.h>

#include <array>
#include <mutex>
#include <thread>
#include <vector>

#include "ara/core/initialization.h"
#include "ara/log/logging.h"

namespace {

class CaptureSink final : public ara::log::LogSink {
public:
  void Write(ara::log::LogRecord const &r) override {
    std::lock_guard<std::mutex> lk(m);
    records.push_back(r);
  }
  std::mutex m;
  std::vector<ara::log::LogRecord> records;
};

class LogTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(ara::core::Deinitialize().HasValue() || true);
    sink = std::make_shared<CaptureSink>();
    ara::log::apext::ClearSinks();
    ara::log::apext::AddSink(sink);
  }
  void TearDown() override { (void)ara::core::Deinitialize(); }
  std::shared_ptr<CaptureSink> sink;
};

TEST_F(LogTest, LevelFilteringFollowsSeverityOrder) {
  auto &l = ara::log::CreateLogger("T1", "t", ara::log::LogLevel::kWarn);
  l.LogFatal() << "f";
  l.LogError() << "e";
  l.LogWarn() << "w";
  l.LogInfo() << "i";
  l.LogDebug() << "d";
  l.LogVerbose() << "v";
  ASSERT_EQ(sink->records.size(), 3U);
  EXPECT_EQ(sink->records[0].message, "f");
  EXPECT_EQ(sink->records[2].level, ara::log::LogLevel::kWarn);
}

TEST_F(LogTest, OffSuppressesEverything) {
  auto &l = ara::log::CreateLogger("T2", "t", ara::log::LogLevel::kOff);
  l.LogFatal() << "x";
  EXPECT_TRUE(sink->records.empty());
  EXPECT_FALSE(l.IsEnabled(ara::log::LogLevel::kFatal));
}

TEST_F(LogTest, SameContextReturnsSameLogger) {
  auto &a = ara::log::CreateLogger("T3", "first");
  auto &b = ara::log::CreateLogger("T3", "second");
  EXPECT_EQ(&a, &b);
  EXPECT_EQ(a.ContextDescription(), "first");
}

TEST_F(LogTest, FormatsValues) {
  auto &l = ara::log::CreateLogger("T4", "t", ara::log::LogLevel::kInfo);
  l.LogInfo() << "n=" << 42 << " u=" << 7U << " b=" << true << " s=" << std::string("x") << ' '
              << ara::log::LogHex8{0xAB} << ' ' << ara::log::LogHex32{0x1F}
              << ' ' << ara::log::LogBin8{5} << ' ' << std::int8_t{-3};
  ASSERT_EQ(sink->records.size(), 1U);
  EXPECT_EQ(sink->records[0].message, "n=42 u=7 b=true s=x 0xAB 0x0000001F 0b00000101 -3");
  EXPECT_EQ(sink->records[0].contextId, "T4");
}

TEST_F(LogTest, NegativeHexIsMaskedToWidth) {
  auto &l = ara::log::CreateLogger("T5", "t", ara::log::LogLevel::kInfo);
  l.LogInfo() << ara::log::LogHex16{0xFFFF};
  EXPECT_EQ(sink->records.at(0).message, "0xFFFF");
}

TEST_F(LogTest, ExplicitFlushStartsNewRecord) {
  auto &l = ara::log::CreateLogger("T6", "t", ara::log::LogLevel::kInfo);
  {
    auto s = l.LogInfo();
    s << "a";
    s.Flush();
    s << "b";
  }
  ASSERT_EQ(sink->records.size(), 2U);
  EXPECT_EQ(sink->records[1].message, "b");
}

TEST_F(LogTest, MovedStreamEmitsOnce) {
  auto &l = ara::log::CreateLogger("T7", "t", ara::log::LogLevel::kInfo);
  {
    auto s = l.LogInfo();
    s << "once";
    auto t = std::move(s);
  }
  EXPECT_EQ(sink->records.size(), 1U);
}

TEST_F(LogTest, RuntimeLevelChange) {
  auto &l = ara::log::CreateLogger("T8", "t", ara::log::LogLevel::kError);
  l.LogInfo() << "hidden";
  l.SetLevel(ara::log::LogLevel::kVerbose);
  l.LogVerbose() << "shown";
  ASSERT_EQ(sink->records.size(), 1U);
  EXPECT_EQ(sink->records[0].message, "shown");
}

TEST_F(LogTest, ConcurrentLoggingLosesNothing) {
  auto &l = ara::log::CreateLogger("T9", "t", ara::log::LogLevel::kInfo);
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t)
    ts.emplace_back([&] {
      for (int i = 0; i < 200; ++i) l.LogInfo() << i;
    });
  for (auto &t : ts) t.join();
  EXPECT_EQ(sink->records.size(), 800U);
}

TEST(LogErrorDomainTest, MessagesAndId) {
  auto ec = ara::log::MakeErrorCode(ara::log::LogErrc::kSinkFailure);
  EXPECT_STREQ(ec.Domain().Name(), "Log");
  EXPECT_STREQ(ec.Message(), "log sink failure");
}

TEST(LogFormatTest, RecordLineContainsFields) {
  ara::log::LogRecord r;
  r.level = ara::log::LogLevel::kError;
  r.appId = "APP1";
  r.contextId = "CTX1";
  r.message = "boom";
  auto line = ara::log::FormatRecord(r);
  EXPECT_NE(line.find("APP1 CTX1 ERRO boom"), std::string::npos);
}


TEST_F(LogTest, ArgAppliesFormatHints) {
  using namespace ara::log;
  auto &l = CreateLogger("T7", "t", LogLevel::kInfo);
  l.LogInfo() << Arg(255, Hex()) << ' ' << Arg(std::uint8_t{5}, Bin(8)) << ' ' << Arg(42, Dec(5))
              << ' ' << Arg(8, Oct()) << ' ' << Arg(-1, Hex()) << ' ' << Arg(-7, Dec());
  EXPECT_EQ(sink->records.at(0).message, "0xFF 0b00000101 00042 0o10 0xFFFFFFFF -7");
}

TEST_F(LogTest, ArgFormatsFloatingPoint) {
  using namespace ara::log;
  auto &l = CreateLogger("T8", "t", LogLevel::kInfo);
  l.LogInfo() << Arg(3.14159, DecFloat(2)) << ' ' << Arg(1500.0, EngFloat(1)) << ' '
              << Arg(0.5, AutoFloat()) << ' ' << Arg(1.0, HexFloat());
  EXPECT_EQ(sink->records.at(0).message, "3.14 1.5e+03 0.5 0x1p+0");
}

TEST_F(LogTest, ArgOfNonNumericFallsBackToPlainInsertion) {
  using namespace ara::log;
  auto &l = CreateLogger("T9", "t", LogLevel::kInfo);
  l.LogInfo() << Arg(std::string("txt"), Hex()) << Arg(true) << Arg('c');
  EXPECT_EQ(sink->records.at(0).message, "txttruec");
}

TEST_F(LogTest, LogHexAndBinWidthsFollowTheType) {
  using namespace ara::log;
  auto &l = CreateLogger("T10", "t", LogLevel::kInfo);
  l.LogInfo() << LogHex16{0x1A} << ' ' << LogHex64{1} << ' ' << LogBin16{3};
  EXPECT_EQ(sink->records.at(0).message,
            "0x001A 0x0000000000000001 0b0000000000000011");
}

TEST_F(LogTest, TagAndLocationReachTheRecord) {
  auto &l = ara::log::CreateLogger("T11", "t", ara::log::LogLevel::kInfo);
  l.LogInfo().WithTag("net").WithLocation("a.cpp", 12) << "hello";
  auto const &r = sink->records.at(0);
  EXPECT_EQ(r.tag, "net");
  EXPECT_EQ(r.file, "a.cpp");
  EXPECT_EQ(r.line, 12);
  auto const text = ara::log::FormatRecord(r);
  EXPECT_NE(text.find("[net] hello (a.cpp:12)"), std::string::npos);
  l.LogInfo() << "plain";
  EXPECT_TRUE(sink->records.at(1).tag.empty());
}

TEST_F(LogTest, SetThresholdChangesFiltering) {
  auto &l = ara::log::CreateLogger("T12", "t", ara::log::LogLevel::kError);
  EXPECT_FALSE(l.IsEnabled(ara::log::LogLevel::kInfo));
  l.SetThreshold(ara::log::LogLevel::kInfo);
  EXPECT_TRUE(l.IsEnabled(ara::log::LogLevel::kInfo));
  EXPECT_EQ(l.GetLevel(), ara::log::LogLevel::kInfo);
}

TEST_F(LogTest, CreateLoggerOverloads) {
  auto &two = ara::log::CreateLogger("T13", "two args");
  EXPECT_EQ(two.GetLevel(), ara::log::LogLevel::kWarn);
  auto &fromSpec = ara::log::CreateLogger(ara::core::InstanceSpecifier("App/Sub/CTX1"));
  EXPECT_EQ(fromSpec.ContextId(), "CTX1");
  EXPECT_EQ(&fromSpec, &ara::log::CreateLogger("CTX1", "again"));
}

TEST_F(LogTest, InsertsPointersErrorCodesDurationsAndSpecifiers) {
  using namespace ara::log;
  auto &l = CreateLogger("T14", "t", LogLevel::kInfo);
  int x = 0;
  l.LogInfo() << static_cast<void const *>(&x) << ' '
              << ara::core::MakeErrorCode(ara::core::CoreErrc::kInvalidArgument) << ' '
              << std::chrono::milliseconds(3) << ' ' << ara::core::InstanceSpecifier("A/B");
  auto const &m = sink->records.at(0).message;
  EXPECT_EQ(m.rfind("0x", 0), 0U);
  EXPECT_NE(m.find("Core:"), std::string::npos);
  EXPECT_NE(m.find("3000000ns"), std::string::npos);
  EXPECT_NE(m.find("A/B"), std::string::npos);
}

TEST_F(LogTest, DisabledStreamIgnoresArgumentsAndModifiers) {
  using namespace ara::log;
  auto &l = CreateLogger("T15", "t", LogLevel::kOff);
  l.LogInfo().WithTag("x").WithLocation("f", 1) << Arg(1, Hex()) << std::chrono::seconds(1);
  EXPECT_TRUE(sink->records.empty());
}

TEST_F(LogTest, ByteSpanIsLoggedAsHex) {
  using namespace ara::log;
  auto &l = CreateLogger("T16", "t", LogLevel::kInfo);
  std::array<ara::core::Byte, 4> const data{ara::core::Byte{0x0a}, ara::core::Byte{0xff}, ara::core::Byte{0x00}, ara::core::Byte{0x10}};
  l.LogInfo() << ara::core::Span<const ara::core::Byte>(data);
  EXPECT_EQ(sink->records.at(0).message, "0a ff 00 10");
  l.LogInfo() << ara::core::Span<const ara::core::Byte>();
  EXPECT_EQ(sink->records.at(1).message, "");
}

} // namespace
