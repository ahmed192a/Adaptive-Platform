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
#include <cstdint>
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
          it->second.state != pkgstate::kTransferred)
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
    return Transition({status::kActivated, status::kRolledBack}, status::kCleaningUp,
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

 private:
  struct Package {
    std::uint64_t size{0};
    std::uint64_t received{0};
    std::uint64_t nextBlock{0};
    PM::SwPackageStateType state{pkgstate::kTransferring};
    bool cancel{false};
  };

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
};

} // namespace ara::ucm::pkgmgr::core

#endif // ARA_UCM_PKGMGR_UCM_CORE_H_
