/**
 * @file update_session.h
 * @brief Transport-independent update-session rules of the State Management
 *        UpdateRequest service interface.
 *
 *  - only one update session may be active at a time  ([SWS_SM_00209],[SWS_SM_00210])
 *  - PrepareUpdate / VerifyUpdate / PrepareRollback / StopUpdateSession are
 *    rejected with kOperationRejected outside an active session ([SWS_SM_00213])
 *  - the session status survives a machine reset ([SWS_SM_00204])
 *
 * Whether ResetMachine itself requires an active session is not stated in the
 * parts of the R25-11 text that could be read; it is implemented as "requires an
 * active session" (see doc/spec-conformance/sm.md).
 */
#ifndef ARA_SM_UPDATE_REQUEST_UPDATE_SESSION_H_
#define ARA_SM_UPDATE_REQUEST_UPDATE_SESSION_H_

#include "ara/sm/sm_error_domain.h"

#include <fstream>
#include <mutex>
#include <optional>
#include <string>

namespace ara::sm::update_request {

class UpdateSession {
 public:
  enum class Phase { kInactive, kAcquired, kPrepared, kVerified, kRollbackPrepared };

  using Result = std::optional<ara::sm::errorDomains>; ///< nullopt == success

  /// [SWS_SM_00203] kNotAllowedMultipleUpdateSessions when one is active.
  Result RequestUpdateSession() {
    std::lock_guard<std::mutex> g(m_);
    if (phase_ != Phase::kInactive) return ara::sm::errorDomains::kNotAllowedMultipleUpdateSessions;
    phase_ = Phase::kAcquired;
    return std::nullopt;
  }

  /// [SWS_SM_00206]
  Result PrepareUpdate() { return InSession(Phase::kPrepared); }
  /// [SWS_SM_00207]
  Result VerifyUpdate() { return InSession(Phase::kVerified); }
  /// [SWS_SM_00208]
  Result PrepareRollback() { return InSession(Phase::kRollbackPrepared); }
  /// [SWS_SM_00205]
  Result StopUpdateSession() { return InSession(Phase::kInactive); }

  /// [SWS_SM_00202] The machine restart itself is done by the caller; this only
  /// decides whether the request is acceptable and records it.
  Result ResetMachine() {
    std::lock_guard<std::mutex> g(m_);
    if (phase_ == Phase::kInactive) return ara::sm::errorDomains::kOperationRejected;
    resetRequested_ = true;
    return std::nullopt;
  }

  bool Active() const {
    std::lock_guard<std::mutex> g(m_);
    return phase_ != Phase::kInactive;
  }
  Phase GetPhase() const {
    std::lock_guard<std::mutex> g(m_);
    return phase_;
  }
  bool ResetRequested() const {
    std::lock_guard<std::mutex> g(m_);
    return resetRequested_;
  }

  /// [SWS_SM_00204] Persist / restore the session status (one small text file).
  bool Save(std::string const &path) const {
    std::lock_guard<std::mutex> g(m_);
    std::ofstream f(path, std::ios::trunc);
    f << static_cast<int>(phase_) << "\n";
    return static_cast<bool>(f);
  }
  bool Load(std::string const &path) {
    std::ifstream f(path);
    int v = -1;
    if (!(f >> v) || v < 0 || v > static_cast<int>(Phase::kRollbackPrepared)) return false;
    std::lock_guard<std::mutex> g(m_);
    phase_ = static_cast<Phase>(v);
    return true;
  }

 private:
  Result InSession(Phase next) {
    std::lock_guard<std::mutex> g(m_);
    if (phase_ == Phase::kInactive) return ara::sm::errorDomains::kOperationRejected;
    phase_ = next;
    if (next == Phase::kInactive) resetRequested_ = false;
    return std::nullopt;
  }

  mutable std::mutex m_;
  Phase phase_{Phase::kInactive};
  bool resetRequested_{false};
};

} // namespace ara::sm::update_request

#endif // ARA_SM_UPDATE_REQUEST_UPDATE_SESSION_H_
