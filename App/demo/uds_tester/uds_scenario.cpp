#include "uds_scenario.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ostream>
#include <sstream>

namespace ap::demo {
using ara::diag::ByteVector;
using ara::diag::uds::Nrc;
using ara::core::Result;

namespace {
ByteVector Bytes(std::string const &s) { return ByteVector(s.begin(), s.end()); }

char const *SidName(unsigned sid) {
  switch (sid) {
  case 0x10: return "DiagnosticSessionControl";
  case 0x11: return "ECUReset";
  case 0x14: return "ClearDiagnosticInformation";
  case 0x19: return "ReadDTCInformation";
  case 0x22: return "ReadDataByIdentifier";
  case 0x23: return "ReadMemoryByAddress";
  case 0x27: return "SecurityAccess";
  case 0x28: return "CommunicationControl";
  case 0x2E: return "WriteDataByIdentifier";
  case 0x31: return "RoutineControl";
  case 0x34: return "RequestDownload";
  case 0x35: return "RequestUpload";
  case 0x36: return "TransferData";
  case 0x37: return "RequestTransferExit";
  case 0x3D: return "WriteMemoryByAddress";
  case 0x3E: return "TesterPresent";
  case 0x85: return "ControlDTCSetting";
  default: return "service";
  }
}

char const *NrcName(unsigned n) {
  switch (n) {
  case 0x10: return "generalReject";
  case 0x11: return "serviceNotSupported";
  case 0x12: return "subFunctionNotSupported";
  case 0x13: return "incorrectMessageLengthOrInvalidFormat";
  case 0x22: return "conditionsNotCorrect";
  case 0x24: return "requestSequenceError";
  case 0x31: return "requestOutOfRange";
  case 0x33: return "securityAccessDenied";
  case 0x35: return "invalidKey";
  case 0x36: return "exceededNumberOfAttempts";
  case 0x37: return "requiredTimeDelayNotExpired";
  case 0x73: return "wrongBlockSequenceCounter";
  case 0x78: return "requestCorrectlyReceived-ResponsePending";
  case 0x7E: return "subFunctionNotSupportedInActiveSession";
  case 0x7F: return "serviceNotSupportedInActiveSession";
  default: return "NRC";
  }
}

ByteVector KeyFor(ByteVector const &seed) {
  ByteVector k = seed;
  for (auto &b : k) b = static_cast<std::uint8_t>(b ^ 0xA5);
  return k;
}

/// Scripted tester: sends, checks the response prefix, prints and records.
struct Tester {
  DemoEcu &ecu;
  std::ostream *out;
  ScenarioResult res;

  std::optional<ByteVector> Send(std::string const &step, ByteVector req, ByteVector const &expectPrefix,
                                 ara::diag::Addressing addr = ara::diag::Addressing::kPhysical) {
    auto r = ecu.Server().Dispatch(req, addr);
    bool ok;
    if (expectPrefix.empty()) {
      ok = !r.has_value(); // "no response expected"
    } else {
      ok = r.has_value() && r->size() >= expectPrefix.size() && std::equal(expectPrefix.begin(), expectPrefix.end(), r->begin());
    }
    TraceLine l{step, req, r, ok, expectPrefix.empty() ? "no response" : HexString(expectPrefix) + "..."};
    (ok ? res.passed : res.failed)++;
    if (out) {
      *out << (ok ? "  [ok]   " : "  [FAIL] ") << step << "\n";
      *out << "      TX  " << HexString(req) << "    " << Describe(req) << "\n";
      if (r) *out << "      RX  " << HexString(*r) << "    " << Describe(*r) << "\n";
      else *out << "      RX  (none)\n";
      if (!ok) *out << "      expected " << l.expectation << "\n";
    }
    res.trace.push_back(std::move(l));
    return r;
  }
  void Section(char const *title) {
    if (out) *out << "\n== " << title << " ==\n";
  }
};
} // namespace

// ----------------------------------------------------------------------------------------------- helpers
std::string HexString(ByteVector const &b) {
  std::string s;
  char buf[4];
  for (std::size_t i = 0; i < b.size(); ++i) {
    std::snprintf(buf, sizeof buf, "%02X", b[i]);
    if (i) s += ' ';
    s += buf;
  }
  return s;
}

std::optional<ByteVector> ParseHex(std::string const &text) {
  std::string digits;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == ',') continue;
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    digits += c;
  }
  if (digits.empty() || digits.size() % 2 != 0) return std::nullopt;
  ByteVector out;
  for (std::size_t i = 0; i < digits.size(); i += 2)
    out.push_back(static_cast<std::uint8_t>(std::stoi(digits.substr(i, 2), nullptr, 16)));
  return out;
}

std::string Describe(ByteVector const &m) {
  char buf[96];
  if (m.empty()) return "empty";
  if (m[0] == 0x7F && m.size() == 3) {
    std::snprintf(buf, sizeof buf, "negative response: %s (0x%02X) for %s", NrcName(m[2]), m[2], SidName(m[1]));
    return buf;
  }
  if (m[0] >= 0x40 && m[0] < 0x7F) {
    std::snprintf(buf, sizeof buf, "positive response: %s", SidName(m[0] - 0x40U));
    return buf;
  }
  std::snprintf(buf, sizeof buf, "%s (0x%02X) request", SidName(m[0]), m[0]);
  return buf;
}

// ----------------------------------------------------------------------------------------------- the ECU
DemoEcu::DemoEcu() : ram_(64, 0x00), flash_() {
  using namespace ara::diag;
  DiagnosticServer::Config cfg;
  cfg.clock = [this] { return now_; };
  server_ = std::make_unique<DiagnosticServer>(cfg);

  DataIdentifier vin;
  vin.read = [] { return Result<ByteVector, Nrc>(Bytes("ADAPTIVEPLATFORM01")); };
  (void)server_->RegisterDataIdentifier(0xF190, vin);

  DataIdentifier sw;
  sw.read = [] { return Result<ByteVector, Nrc>(Bytes("1.4.2")); };
  (void)server_->RegisterDataIdentifier(0xF189, sw);

  DataIdentifier name;
  name.read = [this] { return Result<ByteVector, Nrc>(Bytes(name_)); };
  name.write = [this](ByteVector const &v) {
    name_.assign(v.begin(), v.end());
    return Result<void, Nrc>();
  };
  name.writeLength = 0;
  (void)server_->RegisterDataIdentifier(0xF1A2, name);

  SecurityProvider sp;
  sp.seed = [this](std::uint8_t) { return Result<ByteVector, Nrc>(ByteVector{0x12, 0x34, ++seedCounter_}); };
  sp.validateKey = [](std::uint8_t, ByteVector const &seed, ByteVector const &key) { return key == KeyFor(seed); };
  sp.maxAttempts = 3;
  server_->SetSecurityProvider(sp);

  Routine selfTest; // 0xFF00: pretend to exercise the outputs, report "all ok"
  selfTest.start = [](ByteVector const &) { return Result<ByteVector, Nrc>(ByteVector{0x00}); };
  selfTest.requestResults = [](ByteVector const &) { return Result<ByteVector, Nrc>(ByteVector{0x01, 0x00}); };
  selfTest.sessions = SessionBit(uds::Session::kExtended);
  selfTest.securityLevel = 1;
  (void)server_->RegisterRoutine(0xFF00, selfTest);

  MemoryRegion calib;
  calib.start = 0x20000000;
  calib.size = ram_.size();
  calib.read = [this](std::uint64_t a, std::uint64_t n) {
    auto off = static_cast<std::ptrdiff_t>(a - 0x20000000);
    return Result<ByteVector, Nrc>(ByteVector(ram_.begin() + off, ram_.begin() + off + static_cast<std::ptrdiff_t>(n)));
  };
  calib.write = [this](std::uint64_t a, ByteVector const &d) {
    std::copy(d.begin(), d.end(), ram_.begin() + static_cast<std::ptrdiff_t>(a - 0x20000000));
    return Result<void, Nrc>();
  };
  calib.sessions = SessionBit(uds::Session::kExtended);
  calib.securityLevel = 1;
  (void)server_->RegisterMemoryRegion(calib);

  TransferHandler fw;
  fw.request = [this](TransferDirection, std::uint64_t, std::uint64_t, std::uint8_t) {
    flash_.clear();
    return Result<std::uint32_t, Nrc>(10U); // 8 payload bytes per block
  };
  fw.block = [this](std::uint8_t, ByteVector const &d) {
    flash_.insert(flash_.end(), d.begin(), d.end());
    return Result<ByteVector, Nrc>(ByteVector{});
  };
  fw.securityLevel = 1;
  server_->SetTransferHandler(fw);

  server_->SetCommControlHandler([](std::uint8_t, std::uint8_t) { return true; });
  server_->SetResetHandler([this](std::uint8_t) {
    ++resets_;
    return true;
  });
  server_->RestrictBuiltinService(0x28, SessionBit(uds::Session::kExtended)).HasValue();
}

// ----------------------------------------------------------------------------------------------- the script
ScenarioResult RunScenario(DemoEcu &ecu, std::ostream *out) {
  Tester t{ecu, out, {}};
  if (out) *out << "UDS tester against the demo ECU (in-process, manual S3 clock)\n";

  t.Section("Default session: identification");
  t.Send("start in the default session", {0x10, 0x01}, {0x50, 0x01});
  t.Send("read VIN (F190)", {0x22, 0xF1, 0x90}, {0x62, 0xF1, 0x90, 'A'});
  t.Send("read SW version and an unknown DID in one request", {0x22, 0xF1, 0x89, 0xDE, 0xAD}, {0x62, 0xF1, 0x89});
  t.Send("writing the name needs the extended session", {0x2E, 0xF1, 0xA2, 'X'}, {0x7F, 0x2E, 0x31});
  t.Send("functional TesterPresent with suppressPositiveResponse", {0x3E, 0x80}, {}, ara::diag::Addressing::kFunctional);
  t.Send("functional unknown service gets no answer", {0x99}, {}, ara::diag::Addressing::kFunctional);

  t.Section("Extended session and write");
  t.Send("enter the extended session", {0x10, 0x03}, {0x50, 0x03});
  t.Send("write the vehicle name (F1A2)", [] { ByteVector r{0x2E, 0xF1, 0xA2}; auto n = Bytes("Roadster"); r.insert(r.end(), n.begin(), n.end()); return r; }(), {0x6E, 0xF1, 0xA2});
  t.Send("read it back", {0x22, 0xF1, 0xA2}, {0x62, 0xF1, 0xA2, 'R'});

  t.Section("Security access");
  t.Send("self-test routine is locked", {0x31, 0x01, 0xFF, 0x00}, {0x7F, 0x31, 0x33});
  auto seed = t.Send("request seed (level 1)", {0x27, 0x01}, {0x67, 0x01});
  t.Send("send a wrong key", {0x27, 0x02, 0x00, 0x00, 0x00}, {0x7F, 0x27, 0x35});
  seed = t.Send("request a fresh seed", {0x27, 0x01}, {0x67, 0x01});
  ByteVector key{0x27, 0x02};
  if (seed && seed->size() > 2) {
    auto k = KeyFor(ByteVector(seed->begin() + 2, seed->end()));
    key.insert(key.end(), k.begin(), k.end());
  }
  t.Send("send the right key (seed XOR 0xA5)", key, {0x67, 0x02});

  t.Section("Routine");
  t.Send("start the self-test", {0x31, 0x01, 0xFF, 0x00}, {0x71, 0x01, 0xFF, 0x00, 0x00});
  t.Send("request the results", {0x31, 0x03, 0xFF, 0x00}, {0x71, 0x03, 0xFF, 0x00, 0x01, 0x00});

  t.Section("Diagnostic trouble codes");
  ecu.Server().ReportDtc(0xC10100, true); // U0101 lost communication with TCM
  ecu.Server().SetDtcSnapshot(0xC10100, 1, {{0xF190, Bytes("ADAPTIVEPLATFORM01")}});
  t.Send("count DTCs with testFailed", {0x19, 0x01, 0x01}, {0x59, 0x01, 0x2B, 0x01, 0x00, 0x01});
  t.Send("list DTCs by status mask", {0x19, 0x02, 0x08}, {0x59, 0x02, 0x2B, 0xC1, 0x01, 0x00});
  t.Send("snapshot record 1 of C10100", {0x19, 0x04, 0xC1, 0x01, 0x00, 0x01}, {0x59, 0x04, 0x2B, 0xC1, 0x01, 0x00});
  t.Send("extended data (occurrence counter)", {0x19, 0x06, 0xC1, 0x01, 0x00, 0xFF}, {0x59, 0x06, 0x2B, 0xC1, 0x01, 0x00, 0x2B, 0x01, 0x01});
  t.Send("clear all DTCs", {0x14, 0xFF, 0xFF, 0xFF}, {0x54});
  t.Send("list again: empty", {0x19, 0x02, 0xFF}, {0x59, 0x02, 0x2B});

  t.Section("Memory access");
  t.Send("write calibration RAM", {0x3D, 0x14, 0x20, 0x00, 0x00, 0x00, 0x03, 0xDE, 0xAD, 0xBE}, {0x7D, 0x14});
  t.Send("read it back", {0x23, 0x14, 0x20, 0x00, 0x00, 0x00, 0x03}, {0x63, 0xDE, 0xAD, 0xBE});
  t.Send("outside the region", {0x23, 0x14, 0x30, 0x00, 0x00, 0x00, 0x04}, {0x7F, 0x23, 0x31});

  t.Section("Firmware download");
  t.Send("request download of 12 bytes", {0x34, 0x00, 0x14, 0x08, 0x00, 0x00, 0x00, 0x0C}, {0x74, 0x20, 0x00, 0x0A});
  t.Send("block 1", {0x36, 0x01, 1, 2, 3, 4, 5, 6, 7, 8}, {0x76, 0x01});
  t.Send("block 1 again is a sequence error", {0x36, 0x01, 1}, {0x7F, 0x36, 0x73});
  t.Send("block 2", {0x36, 0x02, 9, 10, 11, 12}, {0x76, 0x02});
  t.Send("request transfer exit", {0x37}, {0x77});

  t.Section("Communication control, reset");
  t.Send("disable Rx and Tx", {0x28, 0x03, 0x01}, {0x68, 0x03});
  t.Send("enable Rx and Tx again", {0x28, 0x00, 0x01}, {0x68, 0x00});
  t.Send("ECU hard reset", {0x11, 0x01}, {0x51, 0x01});
  t.Send("after the reset the session is back to default (0x28 refused)", {0x28, 0x00, 0x01}, {0x7F, 0x28, 0x7F});
  t.Send("name survives the reset", {0x22, 0xF1, 0xA2}, {0x62, 0xF1, 0xA2, 'R'});

  if (out) *out << "\n" << t.res.passed << " checks passed, " << t.res.failed << " failed\n";
  return t.res;
}

} // namespace ap::demo
