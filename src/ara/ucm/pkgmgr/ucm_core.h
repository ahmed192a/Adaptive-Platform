/**
 * @file ucm_core.h
 * @brief Transport-independent UCM PackageManagement logic: the UCM state
 *        machine (CurrentStatus) and the transfer / processing bookkeeping.
 *
 * The SOME/IP skeleton owns file I/O and the flashing hardware; this class owns
 * every *rule* that the AUTOSAR UCM SWS puts on the order of calls and on the
 * ApplicationErrors they raise, so that those rules can be unit-tested without
 * sockets. All methods are thread-safe. Error values are `UCMErrorDomain`
 * enumerators (0 means success).
 *
 * Requirement ids in comments are SWS_UCM_xxxxx as printed in R22-11/R25-11
 * (see doc/spec-conformance/ucm.md for what is verified and what is not).
 */
#ifndef ARA_UCM_PKGMGR_UCM_CORE_H_
#define ARA_UCM_PKGMGR_UCM_CORE_H_

#include "ara/ucm/pkgmgr/ucm_error.h"
#include "ara/ucm/pkgmgr/ucm_types.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace ara::ucm::pkgmgr::core {

namespace PM = ara::ucm::pkgmgr::PackageManagement;

/// PackageManagerStatusType values (CurrentStatus).
namespace status {
constexpr PM::PackageManagerStatusType kIdle = 0x00;
constexpr PM::PackageManagerStatusType kReady = 0x01;
constexpr PM::PackageManagerStatusType kProcessing = 0x02;
constexpr PM::PackageManagerStatusType kActivating = 0x03;
constexpr PM::PackageManagerStatusType kActivated = 0x04;
constexpr PM::PackageManagerStatusType kRollingBack = 0x05;
constexpr PM::PackageManagerStatusType kRolledBack = 0x06;
constexpr PM::PackageManagerStatusType kCleaningUp = 0x07;
constexpr PM::PackageManagerStatusType kVerifying = 0x08;
} // namespace status

/// SwPackageStateType values.
namespace pkgstate {
constexpr PM::SwPackageStateType kTransferring = 0x00;
constexpr PM::SwPackageStateType kTransferred = 0x01;
constexpr PM::SwPackageStateType kProcessing = 0x02;
constexpr PM::SwPackageStateType kProcessed = 0x03;
constexpr PM::SwPackageStateType kProcessingStream = 0x04;
constexpr PM::SwPackageStateType kProcessingFailed = 0x05; ///< [SWS_UCM_00392] (value Unverified)
} // namespace pkgstate

/// SwClusterStateType values. R25-11 names the third state kUpdating; the
/// legacy headers called it kUpdated.
namespace clusterstate {
constexpr PM::SwClusterStateType kPresent = 0x00;
constexpr PM::SwClusterStateType kAdded = 0x01;
constexpr PM::SwClusterStateType kUpdating = 0x02;
constexpr PM::SwClusterStateType kRemoved = 0x03;
} // namespace clusterstate

/// Stable, filesystem-safe textual form of a TransferId (32 lower-case hex
/// digits). Every file the skeleton creates for a package derives from it.
inline std::string IdToString(PM::TransferIdType const &id) {
  std::ostringstream os;
  os << std::hex << std::setfill('0');
  for (auto b : id) os << std::setw(2) << static_cast<unsigned>(b);
  return os.str();
}

struct PackageManagerLimits {
  std::uint64_t maxPackageBytes = 512ULL * 1024 * 1024;
  std::uint32_t blockSize = 1024;
};

/// Parses "0x01", "1" or "01" (as written in sw_packages.json) into a byte.
/// Returns 0 for empty / malformed input and clamps values above 255.
inline std::uint8_t ParseByte(std::string const &text) {
  char *end = nullptr;
  unsigned long v = std::strtoul(text.c_str(), &end, 0);
  if (end == text.c_str()) return 0;
  return static_cast<std::uint8_t>(v > 255UL ? 255UL : v);
}

/// Parses a non-negative decimal number; 0 for empty / malformed input.
inline std::uint64_t ParseU64(std::string const &text) {
  char *end = nullptr;
  unsigned long long v = std::strtoull(text.c_str(), &end, 10);
  return end == text.c_str() ? 0 : static_cast<std::uint64_t>(v);
}

/// Calls UCM makes into State Management's UpdateRequest interface and into the package validation
/// (flash adapter) while activating, verifying and rolling back. A missing hook means "succeeds".
/// Each returns 0 on success or a `UCMErrorDomain` value.
struct UpdateHooks {
  std::function<int()> prepareUpdate;     ///< SM PrepareUpdate / RequestUpdateSession  -> 00258 / 00242
  std::function<int()> verifyUpdate;      ///< SM VerifyUpdate                          -> 00280
  std::function<int()> prepareRollback;   ///< SM PrepareRollback                       -> 00110
  std::function<int()> resetMachine;      ///< SM ResetMachine (after Activate/Rollback)
  std::function<int()> revertPackages;    ///< undo of processed packages               -> 00236
  /// Package consistency / authentication / version check run by TransferExit.
  std::function<int(PM::TransferIdType const &)> validatePackage;
};

/// One line of the UCM history (GetHistory).
struct HistoryEntry {
  std::uint64_t sequence{0};
  std::uint64_t timeSeconds{0};
  std::string action;
  std::string transferId; ///< empty when the action has no package
  int result{0};          ///< UCMErrorDomain value, 0 = success
};

class PackageManagerCore {
 public:
  using Error = int;
  static constexpr Error kOk = 0;
  static constexpr std::uint32_t kDefaultBlockSize = 1024;

  using Limits = PackageManagerLimits;

  struct StartResult {
    Error error{kOk};
    PM::TransferIdType id{};
    std::uint32_t blockSize{0};
  };

  struct DataResult {
    Error error{kOk};
    std::uint64_t accepted{0}; ///< bytes of the block that belong to the package
  };

  PackageManagerCore() = default;
  explicit PackageManagerCore(Limits l) : limits_(l) {}

  PM::PackageManagerStatusType Status() const {
    std::lock_guard<std::mutex> g(m_);
    return status_;
  }

  /// Register a callback invoked (outside the lock) after every status change.
  template <typename F> void OnStatusChange(F f) { notify_ = std::move(f); }

  /// Package state, or -1 when the transfer id is unknown.
  int PackageState(PM::TransferIdType const &id) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = packages_.find(id);
    return it == packages_.end() ? -1 : it->second.state;
  }

  // ---------------------------------------------------------------- transfer
  /// [SWS_UCM_00140] InsufficientMemory when the package cannot be stored.
  StartResult TransferStart(std::uint64_t size, PM::TransferIdType const &newId) {
    std::lock_guard<std::mutex> g(m_);
    StartResult r;
    if (size == 0 || size > limits_.maxPackageBytes) {
      r.error = PM::InsufficientMemory;
      return r;
    }
    Package p;
    p.size = size;
    p.state = pkgstate::kTransferring;
    packages_[newId] = p;
    r.id = newId;
    r.blockSize = limits_.blockSize;
    return r;
  }

  /// Checks in the order of the SWS (00208, 00203, 00204, 00243, 00205).
  /// A block may be shorter than the block size (last block) or padded up to
  /// it; only the bytes still missing are accepted.
  DataResult TransferData(PM::TransferIdType const &id, std::uint64_t dataSize,
                          std::uint64_t blockCounter) {
    std::lock_guard<std::mutex> g(m_);
    DataResult r;
    auto it = packages_.find(id);
    if (it == packages_.end()) { r.error = PM::InvalidTransferId; return r; }  // 00203
    Package &p = it->second;
    if (p.state != pkgstate::kTransferring) {                                   // 00208
      r.error = PM::OperationNotPermitted;
      return r;
    }
    if (blockCounter != p.nextBlock) { r.error = PM::IncorrectBlock; return r; }  // 00204
    if (dataSize > limits_.blockSize) { r.error = PM::IncorrectBlockSize; return r; }  // 00243
    std::uint64_t const remaining = p.size - p.received;
    if (remaining == 0) { r.error = PM::IncorrectSize; return r; }              // 00205
    r.accepted = std::min<std::uint64_t>(dataSize, remaining);
    p.received += r.accepted;
    ++p.nextBlock;
    return r;
  }

  /// [SWS_UCM_00148] OperationNotPermitted, [00212] InvalidTransferId,
  /// [00087] InsufficientData; success sets kTransferred ([00010]).
  Error TransferExit(PM::TransferIdType const &id) {
    std::lock_guard<std::mutex> g(m_);
    auto it = packages_.find(id);
    if (it == packages_.end()) return PM::InvalidTransferId;
    Package &p = it->second;
    if (p.state != pkgstate::kTransferring) return PM::OperationNotPermitted;
    if (p.received < p.size) return PM::InsufficientData;
    if (hooks_.validatePackage) {                    // 00294 / 00098 / 00092 / 00103: the package stays kTransferring
      int const e = hooks_.validatePackage(id);      // so the client can DeleteTransfer or retry.
      if (e != kOk) return e;
    }
    p.state = pkgstate::kTransferred;
    return kOk;
  }

  /// [SWS_UCM_00214] InvalidTransferId, [00215] OperationNotPermitted while the
  /// package is being processed.
  Error DeleteTransfer(PM::TransferIdType const &id) {
    std::lock_guard<std::mutex> g(m_);
    auto it = packages_.find(id);
    if (it == packages_.end()) return PM::InvalidTransferId;
    if (it->second.state == pkgstate::kProcessing ||
        it->second.state == pkgstate::kProcessingStream)
      return PM::OperationNotPermitted;
    packages_.erase(it);
    return kOk;
  }

  // -------------------------------------------------------------- processing
  /// ProcessSwPackage entry checks: [00218] InvalidTransferId, [00017]
  /// ServiceBusy, [00219] OperationNotPermitted.
  Error BeginProcess(PM::TransferIdType const &id) {
    PM::PackageManagerStatusType s;
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = packages_.find(id);
      if (it == packages_.end()) return PM::InvalidTransferId;
      if (status_ == status::kProcessing) return PM::ServiceBusy;
      if ((status_ != status::kIdle && status_ != status::kReady) ||
          (it->second.state != pkgstate::kTransferred && it->second.state != pkgstate::kProcessingFailed))
        return PM::OperationNotPermitted;
      it->second.state = pkgstate::kProcessing;
      it->second.cancel = false;
      status_ = status::kProcessing;
      s = status_;
    }
    Notify(s);
    return kOk;
  }

  /// Finish ProcessSwPackage. Success: package kProcessed, UCM kReady
  /// ([00391]); failure: package back to kTransferred, UCM kReady if another
  /// package is already processed, else kIdle.
  void EndProcess(PM::TransferIdType const &id, bool ok) {
    PM::PackageManagerStatusType s;
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = packages_.find(id);
      if (it != packages_.end())
        it->second.state = ok ? pkgstate::kProcessed : pkgstate::kTransferred;
      status_ = (ok || AnyProcessed()) ? status::kReady : status::kIdle;
      s = status_;
    }
    Notify(s);
  }

  /// [SWS_UCM_00234] OperationNotPermitted, [00235] InvalidTransferId.
  Error Cancel(PM::TransferIdType const &id) {
    std::lock_guard<std::mutex> g(m_);
    auto it = packages_.find(id);
    if (it == packages_.end()) return PM::InvalidTransferId;
    if (it->second.state != pkgstate::kProcessing) return PM::OperationNotPermitted;
    it->second.cancel = true;
    return kOk;
  }

  bool CancelRequested(PM::TransferIdType const &id) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = packages_.find(id);
    return it != packages_.end() && it->second.cancel;
  }

  /// [SWS_UCM_00220] InvalidTransferId.
  Error CheckProgress(PM::TransferIdType const &id) const {
    std::lock_guard<std::mutex> g(m_);
    return packages_.count(id) ? kOk : PM::InvalidTransferId;
  }

  // ---------------------------------------------------- activation / rollback
  /// [SWS_UCM_00241] OperationNotPermitted unless kReady; enters kActivating.
  Error BeginActivate() { return Transition({status::kReady}, status::kActivating, PM::OperationNotPermitted); }

  /// Success: kActivated ([00107]). Failure (e.g. State Management rejected the
  /// update session, [00258]): back to kReady.
  void EndActivate(bool ok) { Set(ok ? status::kActivated : status::kReady); }

  /// [SWS_UCM_00239] OperationNotPermitted unless kActivated or kVerifying.
  Error BeginRollback() {
    return Transition({status::kActivated, status::kVerifying}, status::kRollingBack,
                      PM::OperationNotPermitted);
  }
  void EndRollback(bool ok) { Set(ok ? status::kRolledBack : status::kActivated); }

  /// [SWS_UCM_00237] OperationNotPermitted once activated, i.e. outside
  /// kReady / kProcessing. Processed packages become plain transfers again.
  Error RevertProcessed() {
    PM::PackageManagerStatusType s;
    {
      std::lock_guard<std::mutex> g(m_);
      if (status_ != status::kReady && status_ != status::kProcessing)
        return PM::OperationNotPermitted;
      for (auto &kv : packages_)
        if (kv.second.state == pkgstate::kProcessed) kv.second.state = pkgstate::kTransferred;
      status_ = status::kIdle;
      s = status_;
    }
    Notify(s);
    return kOk;
  }

  /// Finish: only after activation or rollback; runs through kCleaningUp back
  /// to kIdle and forgets every processed package. (Valid source states are
  /// not verified against the SWS text.)
  Error BeginFinish() {
    return Transition({status::kActivated, status::kVerifying, status::kRolledBack}, status::kCleaningUp,
                      PM::OperationNotPermitted);
  }
  void EndFinish() {
    {
      std::lock_guard<std::mutex> g(m_);
      for (auto it = packages_.begin(); it != packages_.end();)
        it = (it->second.state == pkgstate::kProcessed) ? packages_.erase(it) : std::next(it);
    }
    Set(status::kIdle);
  }

  // ------------------------------------------------------------------ hooks
  /// Install the State-Management / validation hooks. Hooks are called while the core's
  /// lock is NOT held (except `validatePackage`, which TransferExit calls under the lock and which
  /// therefore must not call back into the core).
  void SetHooks(UpdateHooks h) { hooks_ = std::move(h); }

  /// Time source for the history (seconds); defaults to the steady clock.
  void SetClock(std::function<std::uint64_t()> c) { clock_ = std::move(c); }

  /// GetId: the identifier of this UCM instance.
  void SetId(std::string id) { id_ = std::move(id); }
  std::string GetId() const { return id_; }

  /// GetHistory: every state-changing operation, oldest first.
  std::vector<HistoryEntry> History() const {
    std::lock_guard<std::mutex> g(hm_);
    return history_;
  }

  /// [SWS_UCM_00003] A cancelled (or otherwise aborted) ProcessSwPackage leaves the package in
  /// kProcessingFailed ([00392]) and UCM back in kReady / kIdle.
  void EndProcessFailed(PM::TransferIdType const &id, int reason) {
    PM::PackageManagerStatusType s;
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = packages_.find(id);
      if (it != packages_.end()) it->second.state = pkgstate::kProcessingFailed;
      status_ = AnyProcessed() ? status::kReady : status::kIdle;
      s = status_;
    }
    Record("ProcessSwPackage", id, reason);
    Notify(s);
  }

  // ------------------------------------------------- activation with hooks
  /// Activate: kReady -> kActivating, State Management PrepareUpdate ([00258] UpdateSessionRejected
  /// or [00242] PrepareUpdateFailed -> PreActivationFailed), then ResetMachine; success = kActivated.
  /// On failure UCM returns to kReady.
  Error Activate() {
    if (int e = BeginActivate()) { Record("Activate", {}, e); return e; }
    int e = hooks_.prepareUpdate ? hooks_.prepareUpdate() : kOk;
    if (e == kOk && hooks_.resetMachine) e = hooks_.resetMachine();
    EndActivate(e == kOk);
    Record("Activate", {}, e);
    return e;
  }

  /// Verify: kActivated -> kVerifying -> (VerifyUpdate) -> kActivated. A failed verification
  /// ([00280] VerificationFailed) stays in kVerifying, where only Rollback is allowed.
  Error Verify() {
    if (int e = Transition({status::kActivated}, status::kVerifying, PM::OperationNotPermitted)) {
      Record("Verify", {}, e);
      return e;
    }
    int const e = hooks_.verifyUpdate ? hooks_.verifyUpdate() : kOk;
    if (e == kOk) Set(status::kActivated);
    Record("Verify", {}, e == kOk ? kOk : static_cast<int>(PM::VerificationFailed));
    return e == kOk ? kOk : static_cast<int>(PM::VerificationFailed);
  }

  /// Rollback with State Management ([00110]): PrepareRollback + ResetMachine. If State
  /// Management cannot, UCM returns to where it was and reports NotAbleToRollback.
  Error Rollback() {
    PM::PackageManagerStatusType const before = Status();
    if (int e = BeginRollback()) { Record("Rollback", {}, e); return e; }
    int e = hooks_.prepareRollback ? hooks_.prepareRollback() : kOk;
    if (e == kOk && hooks_.resetMachine) e = hooks_.resetMachine();
    if (e == kOk) {
      EndRollback(true);
    } else {
      Set(before);
      e = PM::NotAbleToRollback;
    }
    Record("Rollback", {}, e);
    return e;
  }

  /// [SWS_UCM_00236] NotAbleToRevertPackages when the revert hook fails; the packages then stay processed.
  Error Revert() {
    if (hooks_.revertPackages) {
      PM::PackageManagerStatusType const s = Status();
      if (s != status::kReady && s != status::kProcessing) return PM::OperationNotPermitted;
      if (hooks_.revertPackages() != kOk) {
        Record("RevertProcessedSwPackages", {}, PM::NotAbleToRevertPackages);
        return PM::NotAbleToRevertPackages;
      }
    }
    Error const e = RevertProcessed();
    Record("RevertProcessedSwPackages", {}, e);
    return e;
  }

  // ------------------------------------------------------------ persistence
  /// Persist the UCM state ([SWS_UCM_00184] - UCM state must survive a reboot, e.g. the one that
  /// follows ResetMachine). Plain text, one line per item; written atomically via a temp file.
  bool Save(std::string const &path) const {
    std::lock_guard<std::mutex> g(m_);
    std::string const tmp = path + ".tmp";
    {
      std::ofstream f(tmp, std::ios::trunc);
      if (!f) return false;
      f << "ucm-state 1\nstatus " << static_cast<unsigned>(status_) << "\n";
      for (auto const &kv : packages_)
        f << "pkg " << IdToString(kv.first) << ' ' << kv.second.size << ' ' << kv.second.received << ' '
          << kv.second.nextBlock << ' ' << static_cast<unsigned>(kv.second.state) << "\n";
      if (!f) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
  }

  /// Restore a state written by Save(). States that were in flight when the machine went down are
  /// resolved conservatively: processing -> package kProcessingFailed and kReady/kIdle; activating
  /// (the reset itself) -> kActivated (the new software is running; verify next); rolling back ->
  /// kRolledBack; cleaning up -> kIdle. Returns false (state untouched) for a missing / malformed file.
  bool Load(std::string const &path) {
    std::ifstream f(path);
    std::string magic;
    int version = 0;
    if (!(f >> magic >> version) || magic != "ucm-state" || version != 1) return false;
    std::string key;
    unsigned st = 0;
    if (!(f >> key >> st) || key != "status" || st > status::kVerifying) return false;
    std::map<PM::TransferIdType, Package> pk;
    std::string idHex;
    while (f >> key) {
      Package p;
      unsigned ps = 0;
      if (key != "pkg" || !(f >> idHex >> p.size >> p.received >> p.nextBlock >> ps) || idHex.size() != 32 ||
          ps > pkgstate::kProcessingFailed)
        return false;
      PM::TransferIdType id{};
      for (std::size_t i = 0; i < 16; ++i) {
        char *end = nullptr;
        std::string const byte = idHex.substr(i * 2, 2);
        unsigned long v = std::strtoul(byte.c_str(), &end, 16);
        if (end != byte.c_str() + 2) return false;
        id[i] = static_cast<std::uint8_t>(v);
      }
      p.state = static_cast<PM::SwPackageStateType>(ps);
      pk[id] = p;
    }
    PM::PackageManagerStatusType s = static_cast<PM::PackageManagerStatusType>(st);
    {
      std::lock_guard<std::mutex> g(m_);
      packages_ = std::move(pk);
      for (auto &kv : packages_)
        if (kv.second.state == pkgstate::kProcessing || kv.second.state == pkgstate::kProcessingStream)
          kv.second.state = pkgstate::kProcessingFailed;
      switch (s) {
        case status::kProcessing: s = AnyProcessed() ? status::kReady : status::kIdle; break;
        case status::kActivating: s = status::kActivated; break;
        case status::kRollingBack: s = status::kRolledBack; break;
        case status::kCleaningUp: s = status::kIdle; break;
        default: break;
      }
      status_ = s;
    }
    Notify(s);
    return true;
  }

 private:
  struct Package {
    std::uint64_t size{0};
    std::uint64_t received{0};
    std::uint64_t nextBlock{0};
    PM::SwPackageStateType state{pkgstate::kTransferring};
    bool cancel{false};
  };

  void Record(std::string action, PM::TransferIdType const &id, int result) {
    HistoryEntry e;
    e.timeSeconds = clock_ ? clock_() : static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::steady_clock::now().time_since_epoch()).count());
    e.action = std::move(action);
    e.transferId = id == PM::TransferIdType{} ? std::string() : IdToString(id);
    e.result = result;
    std::lock_guard<std::mutex> g(hm_);
    e.sequence = history_.size() + 1;
    history_.push_back(std::move(e));
  }

  bool AnyProcessed() const {
    for (auto const &kv : packages_)
      if (kv.second.state == pkgstate::kProcessed) return true;
    return false;
  }

  Error Transition(std::initializer_list<PM::PackageManagerStatusType> from,
                   PM::PackageManagerStatusType to, Error err) {
    {
      std::lock_guard<std::mutex> g(m_);
      if (std::find(from.begin(), from.end(), status_) == from.end()) return err;
      status_ = to;
    }
    Notify(to);
    return kOk;
  }

  void Set(PM::PackageManagerStatusType s) {
    {
      std::lock_guard<std::mutex> g(m_);
      status_ = s;
    }
    Notify(s);
  }

  void Notify(PM::PackageManagerStatusType s) {
    if (notify_) notify_(s);
  }

  Limits limits_;
  mutable std::mutex m_;
  PM::PackageManagerStatusType status_{status::kIdle};
  std::map<PM::TransferIdType, Package> packages_;
  std::function<void(PM::PackageManagerStatusType)> notify_;
  UpdateHooks hooks_;
  std::function<std::uint64_t()> clock_;
  std::string id_ = "UCM";
  mutable std::mutex hm_;
  std::vector<HistoryEntry> history_;
};

} // namespace ara::ucm::pkgmgr::core

#endif // ARA_UCM_PKGMGR_UCM_CORE_H_
