#include <gtest/gtest.h>

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
              << ara::log::Hex(std::uint8_t{0xAB}) << ' ' << ara::log::Hex(std::uint32_t{0x1F})
              << ' ' << ara::log::Bin(std::uint8_t{5}) << ' ' << std::int8_t{-3};
  ASSERT_EQ(sink->records.size(), 1U);
  EXPECT_EQ(sink->records[0].message, "n=42 u=7 b=true s=x 0xAB 0x0000001F 0b00000101 -3");
  EXPECT_EQ(sink->records[0].contextId, "T4");
}

TEST_F(LogTest, NegativeHexIsMaskedToWidth) {
  auto &l = ara::log::CreateLogger("T5", "t", ara::log::LogLevel::kInfo);
  l.LogInfo() << ara::log::Hex(std::int16_t{-1});
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

} // namespace
