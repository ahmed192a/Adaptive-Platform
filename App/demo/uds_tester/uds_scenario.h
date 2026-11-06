/**
 * @file uds_scenario.h
 * @brief A small demo ECU built on ara::diag and a scripted tester session against it.
 *
 * The same code backs the `uds_tester` command line tool and its unit test.
 */
#ifndef AP_DEMO_UDS_SCENARIO_H_
#define AP_DEMO_UDS_SCENARIO_H_

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ara/diag/diag_facade.h"
#include "ara/diag/diagnostic_server.h"

namespace ap::demo {

/// A diagnostic server populated like a small body-control ECU.
class DemoEcu {
public:
  DemoEcu();
  ara::diag::DiagnosticServer &Server() { return *server_; }
  /// Moves the S3 clock forward (the demo ECU runs on a manual clock so scripts are deterministic).
  void Advance(std::chrono::milliseconds d) { now_ += d; }
  std::string const &Name() const { return name_; }
  std::vector<std::uint8_t> const &FlashImage() const { return flash_; }
  int ResetCount() const { return resets_; }

private:
  std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::time_point{} + std::chrono::hours(1)};
  std::unique_ptr<ara::diag::DiagnosticServer> server_;
  std::string name_{"Demo ECU"};
  std::vector<std::uint8_t> ram_;
  std::vector<std::uint8_t> flash_;
  std::uint8_t seedCounter_{0};
  int resets_{0};
};

struct TraceLine {
  std::string step;
  ara::diag::ByteVector request;
  std::optional<ara::diag::ByteVector> response;
  bool ok{true};
  std::string expectation;
};

struct ScenarioResult {
  std::vector<TraceLine> trace;
  int passed{0};
  int failed{0};
};

/// "22 F1 90" style hex.
std::string HexString(ara::diag::ByteVector const &bytes);
/// Parses "22 F1 90" / "22f190" (spaces and commas ignored).  Empty optional when malformed.
std::optional<ara::diag::ByteVector> ParseHex(std::string const &text);
/// "ReadDataByIdentifier (0x22) request", "negative response: invalidKey (0x35) for SecurityAccess", ...
std::string Describe(ara::diag::ByteVector const &message);

/// Runs the scripted session; when @p out is given a readable trace is printed as it goes.
ScenarioResult RunScenario(DemoEcu &ecu, std::ostream *out);

} // namespace ap::demo

#endif // AP_DEMO_UDS_SCENARIO_H_
