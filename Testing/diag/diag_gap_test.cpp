#include <unistd.h>
// Tests for the gap-closing pass: functional addressing, partial RDBI, ECUReset, DTC memory,
// 0x28 / 0x23 / 0x3D / 0x34-0x37, responsePending, facade classes and persistent DTC storage.
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>

#include "ara/core/instance_specifier.h"
#include "ara/diag/diag_facade.h"
#include "ara/diag/diagnostic_server.h"
#include "ara/diag/per_dtc_storage.h"

using namespace ara::diag;
using uds::Nrc;
using ara::core::Result;
using Clk = std::chrono::steady_clock;

namespace {
ByteVector V(std::initializer_list<int> l) {
  ByteVector v;
  for (int x : l) v.push_back(static_cast<std::uint8_t>(x));
  return v;
}

struct Gap : ::testing::Test {
  Clk::time_point now{Clk::time_point{} + std::chrono::hours(1)};
  DiagnosticServer server{[this] {
    DiagnosticServer::Config c;
    c.clock = [this] { return now; };
    return c;
  }()};
  ByteVector R(ByteVector const &req, Addressing a = Addressing::kPhysical) {
    auto r = server.Dispatch(req, a);
    EXPECT_TRUE(r.has_value());
    return r.value_or(ByteVector{});
  }
  void Extended() { ASSERT_TRUE(server.Dispatch(V({0x10, 0x03})).has_value()); }
};
} // namespace

// ------------------------------------------------------------------ addressing / session / reset
TEST_F(Gap, FunctionalAddressingSuppressesNotSupportedNrcs) {
  EXPECT_EQ(R(V({0x99})), V({0x7F, 0x99, 0x11}));                       // physical: answered
  EXPECT_FALSE(server.Dispatch(V({0x99}), Addressing::kFunctional));    // 0x11 suppressed
  EXPECT_FALSE(server.Dispatch(V({0x10, 0x09}), Addressing::kFunctional)); // 0x12 suppressed
  EXPECT_FALSE(server.Dispatch(V({0x22, 0xAB, 0xCD}), Addressing::kFunctional)); // 0x31 suppressed
  EXPECT_TRUE(server.Dispatch(V({0x22, 0xAB}), Addressing::kFunctional));        // 0x13 still answered
  EXPECT_TRUE(server.Dispatch(V({0x3E, 0x00}), Addressing::kFunctional));        // positive answered
}

TEST_F(Gap, MultiDidReadAnswersTheAvailableOnes) {
  DataIdentifier d;
  d.read = [] { return Result<ByteVector, Nrc>(V({0x42})); };
  ASSERT_TRUE(server.RegisterDataIdentifier(0xF190, d).HasValue());
  EXPECT_EQ(R(V({0x22, 0xF1, 0x90, 0xDE, 0xAD})), V({0x62, 0xF1, 0x90, 0x42})); // 0xDEAD skipped
  EXPECT_EQ(R(V({0x22, 0xDE, 0xAD})), V({0x7F, 0x22, 0x31}));                   // none available
}

TEST_F(Gap, RestrictBuiltinServiceToSessions) {
  ASSERT_TRUE(server.RestrictBuiltinService(0x28, uds::SessionBit(uds::Session::kExtended)).HasValue());
  EXPECT_FALSE(server.RestrictBuiltinService(0x55, kAllSessions).HasValue());
  EXPECT_EQ(R(V({0x28, 0x00, 0x01})), V({0x7F, 0x28, 0x7F}));
  server.SetCommControlHandler([](std::uint8_t, std::uint8_t) { return true; });
  Extended();
  EXPECT_EQ(R(V({0x28, 0x00, 0x01})), V({0x68, 0x00}));
}

TEST_F(Gap, EcuResetRapidShutdownAndSessionFallback) {
  std::vector<int> seen;
  server.SetResetHandler([&](std::uint8_t t) { seen.push_back(t); return true; });
  Extended();
  EXPECT_EQ(R(V({0x11, 0x04})), V({0x51, 0x04, 0x05})); // powerDownTime follows
  EXPECT_EQ(R(V({0x11, 0x05})), V({0x51, 0x05}));
  EXPECT_EQ(R(V({0x11, 0x06})), V({0x7F, 0x11, 0x12}));
  EXPECT_EQ(server.CurrentSession(), uds::Session::kDefault); // a reset ends the diagnostic session
  EXPECT_EQ(seen, (std::vector<int>{4, 5}));
}

TEST_F(Gap, TesterPresentEdgeCases) {
  EXPECT_EQ(R(V({0x3E, 0x00})), V({0x7E, 0x00}));
  EXPECT_FALSE(server.Dispatch(V({0x3E, 0x80})).has_value());
  EXPECT_EQ(R(V({0x3E, 0x01})), V({0x7F, 0x3E, 0x12}));
  EXPECT_EQ(R(V({0x3E})), V({0x7F, 0x3E, 0x13}));
  EXPECT_EQ(R(V({0x3E, 0x00, 0x00})), V({0x7F, 0x3E, 0x13}));
}

// ------------------------------------------------------------------ 0x28
TEST_F(Gap, CommunicationControl) {
  EXPECT_EQ(R(V({0x28, 0x00, 0x01})), V({0x7F, 0x28, 0x11})); // no handler
  std::vector<std::pair<int, int>> calls;
  server.SetCommControlHandler([&](std::uint8_t s, std::uint8_t t) { calls.emplace_back(s, t); return t != 0x03; });
  EXPECT_EQ(R(V({0x28, 0x03, 0x01})), V({0x68, 0x03}));
  EXPECT_FALSE(server.CommControl().rxEnabled);
  EXPECT_FALSE(server.CommControl().txEnabled);
  EXPECT_EQ(R(V({0x28, 0x01, 0x02})), V({0x68, 0x01}));
  EXPECT_TRUE(server.CommControl().rxEnabled);
  EXPECT_FALSE(server.CommControl().txEnabled);
  EXPECT_EQ(server.CommControl().communicationType, 0x02);
  EXPECT_EQ(R(V({0x28, 0x00, 0x03})), V({0x7F, 0x28, 0x22})); // handler refuses
  EXPECT_EQ(R(V({0x28, 0x00, 0x00})), V({0x7F, 0x28, 0x31})); // invalid communicationType
  EXPECT_EQ(R(V({0x28, 0x06, 0x01})), V({0x7F, 0x28, 0x12}));
  EXPECT_EQ(R(V({0x28, 0x04, 0x01})), V({0x7F, 0x28, 0x13})); // 0x04 needs the 2-byte node id
  EXPECT_EQ(R(V({0x28, 0x04, 0x01, 0x00, 0x07})), V({0x68, 0x04}));
  EXPECT_FALSE(server.Dispatch(V({0x28, 0x80, 0x01})).has_value()); // suppressPositiveResponse
}

// ------------------------------------------------------------------ 0x23 / 0x3D
TEST_F(Gap, ReadAndWriteMemoryByAddress) {
  ByteVector ram(16, 0xAA);
  MemoryRegion r;
  r.start = 0x2000;
  r.size = ram.size();
  r.read = [&](std::uint64_t a, std::uint64_t n) {
    return Result<ByteVector, Nrc>(ByteVector(ram.begin() + static_cast<long>(a - 0x2000), ram.begin() + static_cast<long>(a - 0x2000 + n)));
  };
  r.write = [&](std::uint64_t a, ByteVector const &d) {
    std::copy(d.begin(), d.end(), ram.begin() + static_cast<long>(a - 0x2000));
    return Result<void, Nrc>();
  };
  r.securityLevel = 1;
  ASSERT_TRUE(server.RegisterMemoryRegion(r).HasValue());
  EXPECT_FALSE(server.RegisterMemoryRegion(r).HasValue()); // overlap
  // alfid 0x24: 2-byte size, 4-byte address
  EXPECT_EQ(R(V({0x23, 0x24, 0x00, 0x00, 0x20, 0x00, 0x00, 0x02})), V({0x7F, 0x23, 0x33})); // locked
  SecurityProvider sp;
  sp.seed = [](std::uint8_t) { return Result<ByteVector, Nrc>(V({1})); };
  sp.validateKey = [](std::uint8_t, ByteVector const &, ByteVector const &k) { return k == V({1}); };
  server.SetSecurityProvider(sp);
  Extended();
  R(V({0x27, 0x01}));
  EXPECT_EQ(R(V({0x27, 0x02, 0x01})), V({0x67, 0x02}));
  EXPECT_EQ(R(V({0x3D, 0x24, 0x00, 0x00, 0x20, 0x04, 0x00, 0x02, 0x11, 0x22})), V({0x7D, 0x24, 0x00, 0x00, 0x20, 0x04, 0x00, 0x02}));
  EXPECT_EQ(R(V({0x23, 0x24, 0x00, 0x00, 0x20, 0x04, 0x00, 0x02})), V({0x63, 0x11, 0x22}));
  EXPECT_EQ(R(V({0x23, 0x24, 0x00, 0x00, 0x20, 0x0F, 0x00, 0x02})), V({0x7F, 0x23, 0x31})); // crosses the end
  EXPECT_EQ(R(V({0x23, 0x24, 0x00, 0x00, 0x30, 0x00, 0x00, 0x02})), V({0x7F, 0x23, 0x31})); // outside
  EXPECT_EQ(R(V({0x23, 0x24, 0x00, 0x00, 0x20, 0x00, 0x00})), V({0x7F, 0x23, 0x13}));
  EXPECT_EQ(R(V({0x3D, 0x24, 0x00, 0x00, 0x20, 0x00, 0x00, 0x03, 0x01})), V({0x7F, 0x3D, 0x13})); // length != data
  EXPECT_EQ(R(V({0x23, 0x00, 0x20})), V({0x7F, 0x23, 0x13})); // invalid alfid
}

// ------------------------------------------------------------------ 0x34..0x37
namespace {
struct Xfer : Gap {
  ByteVector sink;
  ByteVector source{V({1, 2, 3, 4, 5, 6, 7, 8})};
  bool exited{false};
  void SetUp() override {
    TransferHandler h;
    h.request = [](TransferDirection, std::uint64_t, std::uint64_t, std::uint8_t) { return Result<std::uint32_t, Nrc>(6U); };
    h.block = [this](std::uint8_t seq, ByteVector const &d) -> Result<ByteVector, Nrc> {
      if (!d.empty()) { sink.insert(sink.end(), d.begin(), d.end()); return ByteVector{}; }
      std::size_t off = (static_cast<std::size_t>(seq) - 1) * 4;
      return ByteVector(source.begin() + static_cast<long>(off), source.begin() + static_cast<long>(std::min(off + 4, source.size())));
    };
    h.exit = [this](ByteVector const &) { exited = true; return Result<ByteVector, Nrc>(V({0xAB})); };
    server.SetTransferHandler(h);
    server.Dispatch(V({0x10, 0x02}));
  }
};
} // namespace

TEST_F(Xfer, DownloadFlow) {
  EXPECT_EQ(R(V({0x36, 0x01, 0xAA})), V({0x7F, 0x36, 0x24})); // no transfer open
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10, 0x06})), V({0x74, 0x20, 0x00, 0x06}));
  EXPECT_TRUE(server.TransferActive());
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10, 0x06})), V({0x7F, 0x34, 0x22})); // already open
  EXPECT_EQ(R(V({0x36, 0x02, 0x01})), V({0x7F, 0x36, 0x73}));              // wrong counter
  EXPECT_EQ(R(V({0x36, 0x01, 1, 2, 3, 4})), V({0x76, 0x01}));
  EXPECT_EQ(R(V({0x37})), V({0x7F, 0x37, 0x24}));                           // not everything sent yet
  EXPECT_EQ(R(V({0x36, 0x02, 5, 6})), V({0x76, 0x02}));
  EXPECT_EQ(R(V({0x37})), V({0x77, 0xAB}));
  EXPECT_TRUE(exited);
  EXPECT_FALSE(server.TransferActive());
  EXPECT_EQ(sink, V({1, 2, 3, 4, 5, 6}));
}

TEST_F(Xfer, DownloadRejectsOversizedBlocksAndOverrun) {
  R(V({0x34, 0x00, 0x11, 0x10, 0x04}));
  EXPECT_EQ(R(V({0x36, 0x01, 1, 2, 3, 4, 5})), V({0x7F, 0x36, 0x31})); // block longer than maxBlock (6)
  EXPECT_EQ(R(V({0x36, 0x01, 1, 2, 3, 4})), V({0x76, 0x01}));
  EXPECT_EQ(R(V({0x36, 0x02, 1})), V({0x7F, 0x36, 0x31}));              // more than announced
}

TEST_F(Xfer, UploadFlowAndCounterWrap) {
  EXPECT_EQ(R(V({0x35, 0x00, 0x11, 0x10, 0x08})), V({0x75, 0x20, 0x00, 0x06}));
  EXPECT_EQ(R(V({0x36, 0x01, 0xFF})), V({0x7F, 0x36, 0x13}));            // upload blocks carry no data
  EXPECT_EQ(R(V({0x36, 0x01})), V({0x76, 0x01, 1, 2, 3, 4}));
  EXPECT_EQ(R(V({0x36, 0x02})), V({0x76, 0x02, 5, 6, 7, 8}));
  EXPECT_EQ(R(V({0x37})), V({0x77, 0xAB}));
}

TEST_F(Xfer, SessionChangeCancelsTransfer) {
  R(V({0x34, 0x00, 0x11, 0x10, 0x06}));
  R(V({0x10, 0x03}));
  EXPECT_FALSE(server.TransferActive());
  server.Dispatch(V({0x10, 0x01})); // default session: transfer service not allowed
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10, 0x06})), V({0x7F, 0x34, 0x7F}));
}

TEST_F(Gap, TransferWithoutHandlerAndBadFormats) {
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10, 0x06})), V({0x7F, 0x34, 0x11}));
  TransferHandler h;
  h.request = [](TransferDirection, std::uint64_t, std::uint64_t, std::uint8_t) { return Result<std::uint32_t, Nrc>(2U); };
  server.SetTransferHandler(h);
  server.Dispatch(V({0x10, 0x02}));
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10})), V({0x7F, 0x34, 0x13}));
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10, 0x00})), V({0x7F, 0x34, 0x31})); // zero size
  EXPECT_EQ(R(V({0x34, 0x00, 0x11, 0x10, 0x06})), V({0x7F, 0x34, 0x10})); // maxBlock < 3
}

// ------------------------------------------------------------------ responsePending
TEST_F(Gap, ResponsePendingIsPolledAndForwarded) {
  std::vector<ByteVector> pendings;
  server.SetResponsePendingSink([&](ByteVector const &r) { pendings.push_back(r); });
  int polls = 0;
  ASSERT_TRUE(server.RegisterService(0x3C, kAllSessions, [&](ByteVector const &) -> Result<ByteVector, Nrc> {
    if (++polls < 3) return Result<ByteVector, Nrc>::FromError(Nrc::kResponsePending);
    return ByteVector{0x01};
  }).HasValue());
  EXPECT_EQ(R(V({0x3C})), V({0x7C, 0x01}));
  EXPECT_EQ(polls, 3);
  EXPECT_EQ(pendings, (std::vector<ByteVector>{V({0x7F, 0x3C, 0x78}), V({0x7F, 0x3C, 0x78})}));
}

TEST_F(Gap, ResponsePendingGivesUpAfterMaxRetries) {
  DiagnosticServer::Config c;
  c.clock = [this] { return now; };
  c.maxPendingRetries = 2;
  DiagnosticServer s(c);
  int sinks = 0;
  s.SetResponsePendingSink([&](ByteVector const &) { ++sinks; });
  Routine r;
  r.start = [](ByteVector const &) { return Result<ByteVector, Nrc>::FromError(Nrc::kResponsePending); };
  ASSERT_TRUE(s.RegisterRoutine(0x0100, r).HasValue());
  EXPECT_EQ(*s.Dispatch(V({0x31, 0x01, 0x01, 0x00})), V({0x7F, 0x31, 0x78})); // still pending: final NRC goes out
  EXPECT_EQ(sinks, 2);
}

// ------------------------------------------------------------------ DTC memory
TEST_F(Gap, DtcSnapshotExtendedDataAndOccurrenceCounter) {
  server.ReportDtc(0x123456, true);
  server.ReportDtc(0x123456, true); // still failed: no new occurrence
  server.ReportDtc(0x123456, false);
  server.ReportDtc(0x123456, true); // second occurrence
  EXPECT_EQ(server.DtcInfo(0x123456)->occurrences, 2);
  server.SetDtcSnapshot(0x123456, 1, {{0xF190, V({0xAA, 0xBB})}, {0x0100, V({0x07})}});
  server.SetDtcExtendedData(0x123456, 0x05, V({0x09}));
  server.SetDtcSnapshot(0x123456, 2, {});

  EXPECT_EQ(R(V({0x19, 0x03})), V({0x59, 0x03, 0x12, 0x34, 0x56, 0x01, 0x12, 0x34, 0x56, 0x02}));
  // by DTC number, record 1
  EXPECT_EQ(R(V({0x19, 0x04, 0x12, 0x34, 0x56, 0x01})),
            V({0x59, 0x04, 0x2B, 0x12, 0x34, 0x56, 0x2B, 0x01, 0x02, 0xF1, 0x90, 0xAA, 0xBB, 0x01, 0x00, 0x07}));
  // all records (0xFF): record 2 is empty (zero identifiers)
  auto all = R(V({0x19, 0x04, 0x12, 0x34, 0x56, 0xFF}));
  EXPECT_EQ(all.size(), 7U + 9U + 2U);
  EXPECT_EQ(all[all.size() - 2], 0x02);
  EXPECT_EQ(all.back(), 0x00);
  // extended data: record 0x01 (occurrence counter, synthesised) and 0x05
  EXPECT_EQ(R(V({0x19, 0x06, 0x12, 0x34, 0x56, 0xFF})),
            V({0x59, 0x06, 0x2B, 0x12, 0x34, 0x56, 0x2B, 0x01, 0x02, 0x05, 0x09}));
  EXPECT_EQ(R(V({0x19, 0x06, 0x12, 0x34, 0x56, 0x05})), V({0x59, 0x06, 0x2B, 0x12, 0x34, 0x56, 0x2B, 0x05, 0x09}));
  EXPECT_EQ(R(V({0x19, 0x04, 0x65, 0x43, 0x21, 0x01})), V({0x7F, 0x19, 0x31})); // unknown DTC
  EXPECT_EQ(R(V({0x19, 0x04, 0x12, 0x34, 0x56})), V({0x7F, 0x19, 0x13}));
  EXPECT_EQ(R(V({0x19, 0x03, 0x00})), V({0x7F, 0x19, 0x13}));
}

TEST_F(Gap, ClearByGroupOfDtc) {
  server.ReportDtc(0x000101, true);
  server.ReportDtc(0x000102, true);
  server.ReportDtc(0x000201, true);
  server.RegisterDtcGroup(0x000100, {0x000101, 0x000102});
  EXPECT_EQ(R(V({0x14, 0x00, 0x01, 0x00})), V({0x54}));
  EXPECT_FALSE(server.DtcStatus(0x000101).has_value());
  EXPECT_FALSE(server.DtcStatus(0x000102).has_value());
  EXPECT_TRUE(server.DtcStatus(0x000201).has_value());
  EXPECT_EQ(R(V({0x14, 0x00, 0x09, 0x09})), V({0x7F, 0x14, 0x31}));
}

namespace {
struct PerFixture : ::testing::Test {
  std::filesystem::path dir;
  ara::per::SharedHandle<ara::per::KeyValueStorage> kvs;
  void SetUp() override {
    dir = std::filesystem::temp_directory_path() / ("diag_gap_" + std::to_string(::getpid()) + "_" +
                                                     ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(dir);
    std::string cfg = R"({"storageRoot":")" + (dir / "per").string() +
                      R"(","storages":[{"specifier":"Diag/Dtc","type":"kvs","maxSize":65536}]})";
    auto pc = ara::per::PersistencyConfig::FromJson(cfg);
    ASSERT_TRUE(pc.HasValue());
    ASSERT_TRUE(ara::per::Configure(std::move(pc).Value()).HasValue());
    auto h = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("Diag/Dtc"));
    ASSERT_TRUE(h.HasValue());
    kvs = h.Value();
  }
  void TearDown() override { std::filesystem::remove_all(dir); }
};
} // namespace

TEST_F(PerFixture, DtcMemorySurvivesARestart) {
  {
    DiagnosticServer a;
    a.SetDtcStorage(MakePerDtcStorage(kvs));
    a.ReportDtc(0xABCDEF, true);
    a.SetDtcSnapshot(0xABCDEF, 1, {{0xF190, V({0x01, 0x02})}});
    a.SetDtcExtendedData(0xABCDEF, 0x10, V({0x7F}));
  }
  DiagnosticServer b; // "restart": fresh server, same storage
  EXPECT_FALSE(b.DtcStatus(0xABCDEF).has_value());
  b.SetDtcStorage(MakePerDtcStorage(kvs));
  auto info = b.DtcInfo(0xABCDEF);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->status, 0x2B);
  EXPECT_EQ(info->occurrences, 1);
  EXPECT_EQ(info->snapshots.at(1), V({0x01, 0xF1, 0x90, 0x01, 0x02}));
  EXPECT_EQ(info->extendedData.at(0x10), V({0x7F}));
  // clearing is persisted as well
  b.Dispatch(V({0x14, 0xFF, 0xFF, 0xFF}));
  DiagnosticServer c;
  c.SetDtcStorage(MakePerDtcStorage(kvs));
  EXPECT_FALSE(c.DtcStatus(0xABCDEF).has_value());
}

TEST(DtcCodec, RoundTripAndMalformedInput) {
  DtcMemory m;
  m[0x010203].status = 0x08;
  m[0x010203].occurrences = 7;
  m[0x010203].snapshots[3] = V({0xAA});
  m[0x010203].extendedData[9] = {};
  auto back = DecodeDtcMemory(EncodeDtcMemory(m));
  ASSERT_EQ(back.size(), 1U);
  EXPECT_EQ(back[0x010203].status, 0x08);
  EXPECT_EQ(back[0x010203].occurrences, 7);
  EXPECT_EQ(back[0x010203].snapshots[3], V({0xAA}));
  EXPECT_TRUE(back[0x010203].extendedData.count(9));
  // garbage lines are skipped, good ones survive
  auto mixed = DecodeDtcMemory("zzz\n010203 08 1 s1=ZZ s2=0A\n01 02\n010204 FFF 1\n");
  ASSERT_EQ(mixed.size(), 1U);
  EXPECT_EQ(mixed[0x010203].snapshots.count(1), 0U);
  EXPECT_EQ(mixed[0x010203].snapshots.at(2), V({0x0A}));
  EXPECT_TRUE(DecodeDtcMemory("").empty());
}

// ------------------------------------------------------------------ facade
namespace {
struct Echo : GenericUDSService {
  using GenericUDSService::GenericUDSService;
  ServiceResult HandleMessage(ByteVector const &a) override { return ServiceResult(a); }
};
struct Calib : GenericRoutine {
  using GenericRoutine::GenericRoutine;
  Result<ByteVector, Nrc> Start(ByteVector const &) override { return ByteVector{0x01}; }
};
struct Vin : GenericDataIdentifier {
  ByteVector stored{V({'V', '1'})};
  using GenericDataIdentifier::GenericDataIdentifier;
  Result<ByteVector, Nrc> Read() override { return stored; }
  bool IsWritable() const override { return true; }
  Result<void, Nrc> Write(ByteVector const &v) override { stored = v; return {}; }
};
} // namespace

TEST_F(Gap, GenericUdsServiceOfferStopOffer) {
  Conversation conv(server, 7);
  EXPECT_EQ(conv.GetConversationIdentifier(), 7U);
  {
    Echo svc(server, 0x3C);
    EXPECT_FALSE(svc.StopOffer().HasValue()); // not offered yet
    ASSERT_TRUE(svc.Offer().HasValue());
    EXPECT_FALSE(svc.Offer().HasValue());
    EXPECT_EQ(*conv.Request(V({0x3C, 0x09, 0x08})), V({0x7C, 0x09, 0x08}));
    Echo clash(server, 0x3C);
    EXPECT_FALSE(clash.Offer().HasValue());
  } // destructor stops the offer
  EXPECT_EQ(*conv.Request(V({0x3C})), V({0x7F, 0x3C, 0x11}));
}

TEST_F(Gap, GenericRoutineAndDataIdentifier) {
  Conversation conv(server, 1);
  Calib routine(server, 0x0F00);
  ASSERT_TRUE(routine.Offer().HasValue());
  EXPECT_EQ(*conv.Request(V({0x31, 0x01, 0x0F, 0x00})), V({0x71, 0x01, 0x0F, 0x00, 0x01}));
  EXPECT_EQ(*conv.Request(V({0x31, 0x02, 0x0F, 0x00})), V({0x7F, 0x31, 0x12})); // default Stop()
  ASSERT_TRUE(routine.StopOffer().HasValue());
  EXPECT_EQ(*conv.Request(V({0x31, 0x01, 0x0F, 0x00})), V({0x7F, 0x31, 0x31}));

  Vin vin(server, 0xF190);
  ASSERT_TRUE(vin.Offer().HasValue());
  EXPECT_EQ(*conv.Request(V({0x22, 0xF1, 0x90})), V({0x62, 0xF1, 0x90, 'V', '1'}));
  conv.Request(V({0x10, 0x03}));
  EXPECT_EQ(conv.GetActiveSession(), uds::Session::kExtended);
  EXPECT_EQ(*conv.Request(V({0x2E, 0xF1, 0x90, 'V', '2'})), V({0x6E, 0xF1, 0x90}));
  EXPECT_EQ(vin.stored, V({'V', '2'}));
  ASSERT_TRUE(vin.StopOffer().HasValue());
  EXPECT_EQ(*conv.Request(V({0x22, 0xF1, 0x90})), V({0x7F, 0x22, 0x31}));
  EXPECT_EQ(conv.GetActiveSecurityLevel(), 0);
}

TEST_F(Gap, MonitorDebouncingAndReporting) {
  Monitor m(server, 0x00AA55, {3, 2});
  EXPECT_FALSE(m.ReportMonitorAction(MonitorAction::kFailed).HasValue()); // not offered
  ASSERT_TRUE(m.Offer().HasValue());
  EXPECT_FALSE(m.Offer().HasValue());
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kPrefailed).HasValue());
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kPrefailed).HasValue());
  EXPECT_FALSE(server.DtcStatus(0x00AA55).has_value());      // threshold not reached yet
  EXPECT_GT(m.GetFaultDetectionCounter(), 0);
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kFreezeDebouncing).HasValue());
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kPrefailed).HasValue());
  EXPECT_FALSE(server.DtcStatus(0x00AA55).has_value());      // frozen: ignored
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kResetDebouncing).HasValue());
  EXPECT_EQ(m.GetFaultDetectionCounter(), 0);
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kPrefailed).HasValue());
  EXPECT_EQ(server.DtcStatus(0x00AA55).value() & dtc::kTestFailed, dtc::kTestFailed);
  EXPECT_EQ(m.GetFaultDetectionCounter(), 127);
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kPrepassed).HasValue());
  EXPECT_NE(server.DtcStatus(0x00AA55).value() & dtc::kTestFailed, 0);
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kPrepassed).HasValue());
  EXPECT_EQ(server.DtcStatus(0x00AA55).value() & dtc::kTestFailed, 0);
  EXPECT_EQ(m.GetFaultDetectionCounter(), -128);
  ASSERT_TRUE(m.ReportMonitorAction(MonitorAction::kFailed).HasValue());
  EXPECT_EQ(m.GetFaultDetectionCounter(), 127);
  ASSERT_TRUE(m.StopOffer().HasValue());
  EXPECT_FALSE(m.ReportMonitorAction(MonitorAction::kPassed).HasValue());
}
