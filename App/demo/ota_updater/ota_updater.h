/**
 * @file ota_updater.h
 * @brief Demo application: a complete software update driven through the UCM PackageManagement
 *        rules (ara::ucm::pkgmgr::core::PackageManagerCore) with ara::sm State Management on the
 *        other side (UpdateSession + a configurable StateMachine).
 *
 * Everything runs in-process (no SOME/IP sockets), so the whole flow is deterministic and testable:
 * TransferStart -> TransferData (blocks) -> TransferExit (package digest check) -> ProcessSwPackage ->
 * Activate (State Management PrepareUpdate / ResetMachine) -> Verify -> Finish, or Rollback on failure.
 */
#ifndef AP_DEMO_OTA_UPDATER_H_
#define AP_DEMO_OTA_UPDATER_H_

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace ap::demo {

struct UpdateOptions {
  std::uint64_t packageSize{5000}; ///< size of the generated package (ignored with packageFile)
  std::string packageFile;         ///< read the package from this file instead of generating one
  bool tamper{false};              ///< flip one byte in transit: the digest check must reject it
  bool rejectSession{false};       ///< State Management refuses the update session
  bool failVerify{false};          ///< the new software fails verification -> rollback
  std::string stateFile;           ///< persist the UCM state here when the machine "resets"
  bool quiet{false};               ///< do not print the progress bars
};

enum class Outcome { kUpdated, kRolledBack, kPackageRejected, kSessionRejected, kError };

struct UpdateReport {
  Outcome outcome{Outcome::kError};
  int ucmError{0};                         ///< last UCMErrorDomain value that stopped the flow (0 = none)
  std::string ucmStatus;                  ///< final CurrentStatus, e.g. "kIdle"
  std::string machineState;               ///< final state of the machine state machine
  std::vector<std::string> statusTrail;   ///< every CurrentStatus the UCM went through
  std::vector<std::string> machineTrail;  ///< every machine state the State Management went through
  std::uint64_t blocks{0};
  std::string message;
};

/// Run one update; progress is printed to `out` unless options.quiet.
UpdateReport RunUpdate(UpdateOptions const &options, std::ostream &out);

/// Process exit code for a report: 0 updated, 2 rolled back, 3 package rejected, 4 session rejected, 1 error.
int ExitCode(UpdateReport const &report);

char const *ToString(Outcome o);

} // namespace ap::demo

#endif // AP_DEMO_OTA_UPDATER_H_
