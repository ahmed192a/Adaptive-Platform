#include "ota_updater.h"

#include "ara/sm/state_machine/state_machine.h"
#include "ara/sm/update_request/update_session.h"
#include "ara/ucm/pkgmgr/ucm_core.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>

namespace ap::demo {

namespace {

namespace core = ara::ucm::pkgmgr::core;
namespace PM = ara::ucm::pkgmgr::PackageManagement;
namespace sm = ara::sm::state_machine;

char const *StatusName(PM::PackageManagerStatusType s) {
  switch (s) {
  case core::status::kIdle: return "kIdle";
  case core::status::kReady: return "kReady";
  case core::status::kProcessing: return "kProcessing";
  case core::status::kActivating: return "kActivating";
  case core::status::kActivated: return "kActivated";
  case core::status::kRollingBack: return "kRollingBack";
  case core::status::kRolledBack: return "kRolledBack";
  case core::status::kCleaningUp: return "kCleaningUp";
  case core::status::kVerifying: return "kVerifying";
  default: return "?";
  }
}

char const *ErrorName(int e) {
  switch (e) {
  case 0: return "ok";
  case PM::InsufficientMemory: return "InsufficientMemory";
  case PM::PackageInconsistent: return "PackageInconsistent";
  case PM::AuthenticationFailed: return "AuthenticationFailed";
  case PM::OperationNotPermitted: return "OperationNotPermitted";
  case PM::UpdateSessionRejected: return "UpdateSessionRejected";
  case PM::VerificationFailed: return "VerificationFailed";
  case PM::NotAbleToRollback: return "NotAbleToRollback";
  default: return "error";
  }
}

// FNV-1a, 64 bit: stands in for the signature/digest a real package manifest carries.
std::uint64_t Digest(std::vector<std::uint8_t> const &data) {
  std::uint64_t h = 1469598103934665603ULL;
  for (auto b : data) {
    h ^= b;
    h *= 1099511628211ULL;
  }
  return h;
}

std::vector<std::uint8_t> GeneratePackage(std::uint64_t size) {
  std::vector<std::uint8_t> p(static_cast<std::size_t>(size));
  std::uint32_t x = 0x12345678u;
  for (auto &b : p) {
    x = x * 1664525u + 1013904223u;
    b = static_cast<std::uint8_t>(x >> 24);
  }
  return p;
}

void Bar(std::ostream &out, char const *label, std::uint64_t done, std::uint64_t total) {
  int const pct = total == 0 ? 100 : static_cast<int>(done * 100 / total);
  out << "  " << std::left << std::setw(11) << label << " [";
  for (int i = 0; i < 20; ++i) out << (i < pct / 5 ? '#' : '.');
  out << "] " << std::setw(3) << std::right << pct << "%\n";
}

} // namespace

char const *ToString(Outcome o) {
  switch (o) {
  case Outcome::kUpdated: return "updated";
  case Outcome::kRolledBack: return "rolled-back";
  case Outcome::kPackageRejected: return "package-rejected";
  case Outcome::kSessionRejected: return "session-rejected";
  case Outcome::kError: return "error";
  }
  return "error";
}

int ExitCode(UpdateReport const &r) {
  switch (r.outcome) {
  case Outcome::kUpdated: return 0;
  case Outcome::kRolledBack: return 2;
  case Outcome::kPackageRejected: return 3;
  case Outcome::kSessionRejected: return 4;
  case Outcome::kError: return 1;
  }
  return 1;
}

UpdateReport RunUpdate(UpdateOptions const &opt, std::ostream &out) {
  UpdateReport rep;
  std::ostringstream sink;
  std::ostream &log = opt.quiet ? sink : out;

  // ----------------------------------------------------------------- package
  std::vector<std::uint8_t> package;
  if (!opt.packageFile.empty()) {
    std::ifstream f(opt.packageFile, std::ios::binary);
    if (!f) {
      rep.message = "cannot read package file " + opt.packageFile;
      return rep;
    }
    package.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  } else {
    package = GeneratePackage(opt.packageSize);
  }
  if (package.empty()) {
    rep.message = "empty package";
    return rep;
  }
  std::uint64_t const expectedDigest = Digest(package); // what the manifest promises

  // ------------------------------------------------- State Management (the other side)
  sm::StateManager manager;
  sm::StateMachine &machine = manager.Add(
      "Machine", {"Initial", "Off", "Driving", "PrepareUpdate", "VerifyUpdate", "PrepareRollback"}, "Initial");
  machine.AddTransition("Initial", "Driving");
  machine.AddTransition("Driving", "PrepareUpdate");
  machine.AddTransition("PrepareUpdate", "VerifyUpdate");
  machine.AddTransition("VerifyUpdate", "Driving");
  machine.AddTransition("VerifyUpdate", "PrepareRollback");
  machine.AddTransition("PrepareUpdate", "PrepareRollback");
  machine.AddTransition("PrepareRollback", "Driving");
  machine.AddTransition("*", "Off");
  machine.SetNotifier([&](std::string const &from, std::string const &to) {
    rep.machineTrail.push_back(to);
    log << "  [SM]  machine state " << from << " -> " << to << "\n";
  });
  rep.machineTrail.push_back("Initial");
  manager.Start("Machine", "Driving");
  ara::sm::update_request::UpdateSession session;

  // ------------------------------------------------------------------ UCM core
  core::PackageManagerCore ucm;
  ucm.SetId("ucm-demo");
  rep.statusTrail.push_back(StatusName(ucm.Status()));
  ucm.OnStatusChange([&](PM::PackageManagerStatusType s) {
    rep.statusTrail.push_back(StatusName(s));
    log << "  [UCM] CurrentStatus = " << StatusName(s) << "\n";
  });

  std::map<PM::TransferIdType, std::vector<std::uint8_t>> received; // what the UCM stored on "flash"
  core::UpdateHooks hooks;
  hooks.validatePackage = [&](PM::TransferIdType const &id) {
    auto it = received.find(id);
    return it != received.end() && Digest(it->second) == expectedDigest ? 0 : static_cast<int>(PM::PackageInconsistent);
  };
  hooks.prepareUpdate = [&]() {
    if (opt.rejectSession) return static_cast<int>(PM::UpdateSessionRejected);
    if (session.RequestUpdateSession() || session.PrepareUpdate()) return static_cast<int>(PM::UpdateSessionRejected);
    return machine.RequestTransition("PrepareUpdate") ? static_cast<int>(PM::PreActivationFailed) : 0;
  };
  hooks.resetMachine = [&]() {
    if (session.ResetMachine()) return static_cast<int>(PM::PreActivationFailed);
    log << "  [SM]  ResetMachine requested; UCM state is persisted across the restart\n";
    if (!opt.stateFile.empty() && !ucm.Save(opt.stateFile)) return static_cast<int>(PM::PersistencyAllocationFailed);
    return 0;
  };
  hooks.verifyUpdate = [&]() {
    if (session.VerifyUpdate() || machine.RequestTransition("VerifyUpdate")) return 1;
    return opt.failVerify ? 1 : 0;
  };
  hooks.prepareRollback = [&]() {
    if (session.PrepareRollback() || machine.RequestTransition("PrepareRollback")) return 1;
    return 0;
  };
  ucm.SetHooks(hooks);

  auto fail = [&](Outcome o, int err, std::string msg) {
    rep.outcome = o;
    rep.ucmError = err;
    rep.message = std::move(msg);
    rep.ucmStatus = StatusName(ucm.Status());
    rep.machineState = machine.CurrentState();
    log << "  result: " << rep.message << "\n";
    return rep;
  };

  // ----------------------------------------------------------------- transfer
  PM::TransferIdType id{};
  for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<std::uint8_t>(0xA0 + i);
  log << "1. Transfer " << package.size() << " bytes\n";
  auto start = ucm.TransferStart(package.size(), id);
  if (start.error != 0) return fail(Outcome::kError, start.error, std::string("TransferStart: ") + ErrorName(start.error));
  auto &stored = received[id];
  std::uint64_t sent = 0;
  std::uint64_t block = 0;
  std::uint64_t const total = package.size();
  while (sent < total) {
    std::uint64_t const n = std::min<std::uint64_t>(start.blockSize, total - sent);
    std::vector<std::uint8_t> chunk(package.begin() + static_cast<std::ptrdiff_t>(sent),
                                    package.begin() + static_cast<std::ptrdiff_t>(sent + n));
    if (opt.tamper && block == 0) chunk[0] = static_cast<std::uint8_t>(chunk[0] ^ 0xFF); // corrupted in transit
    auto r = ucm.TransferData(id, n, block);
    if (r.error != 0) return fail(Outcome::kError, r.error, std::string("TransferData: ") + ErrorName(r.error));
    stored.insert(stored.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(r.accepted));
    sent += r.accepted;
    ++block;
    if (!opt.quiet && (block % 4 == 0 || sent == total)) Bar(log, "transfer", sent, total);
  }
  rep.blocks = block;
  int e = ucm.TransferExit(id);
  if (e != 0) {
    ucm.DeleteTransfer(id);
    return fail(Outcome::kPackageRejected, e,
                std::string("TransferExit: ") + ErrorName(e) + " (digest mismatch) - transfer deleted");
  }
  log << "2. Package verified (digest 0x" << std::hex << expectedDigest << std::dec << ")\n";

  // --------------------------------------------------------------- processing
  log << "3. ProcessSwPackage\n";
  e = ucm.BeginProcess(id);
  if (e != 0) return fail(Outcome::kError, e, std::string("ProcessSwPackage: ") + ErrorName(e));
  {
    std::uint64_t n = 0;
    for (char const *step : {"unpack", "check", "write flash"}) Bar(log, step, ++n, 3);
  }
  ucm.EndProcess(id, true);

  // --------------------------------------------------------------- activation
  log << "4. Activate (State Management: PrepareUpdate, ResetMachine)\n";
  e = ucm.Activate();
  if (e != 0) {
    ucm.Revert();
    session.StopUpdateSession();
    return fail(Outcome::kSessionRejected, e, std::string("Activate: ") + ErrorName(e) + " - packages reverted");
  }
  log << "5. Verify the new software\n";
  e = ucm.Verify();
  if (e != 0) {
    log << "   verification failed -> Rollback\n";
    int const rb = ucm.Rollback();
    machine.RequestTransition("Driving");
    session.StopUpdateSession();
    ucm.BeginFinish();
    ucm.EndFinish();
    if (rb != 0) return fail(Outcome::kError, rb, std::string("Rollback: ") + ErrorName(rb));
    return fail(Outcome::kRolledBack, e, "update rolled back: " + std::string(ErrorName(e)));
  }
  log << "6. Finish\n";
  ucm.BeginFinish();
  ucm.EndFinish();
  machine.RequestTransition("Driving");
  session.StopUpdateSession();

  rep.outcome = Outcome::kUpdated;
  rep.message = "update installed";
  rep.ucmStatus = StatusName(ucm.Status());
  rep.machineState = machine.CurrentState();
  log << "  result: " << rep.message << "\n";
  return rep;
}

} // namespace ap::demo
