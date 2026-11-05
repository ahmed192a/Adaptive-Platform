/**
 * @file recovery_action.h
 * @brief ara::phm::RecoveryAction (SWS_PHM) - State Management's recovery callback.
 *
 * In R24-11 the class is final; the handler is supplied at construction.
 * While offered, PHM invokes the handler when a global supervision that
 * references this action expires.  Returning (a Future holding) an error -
 * typically PhmErrc::kSMCanNotHandleRecovery - makes PHM fire the watchdog.
 */
#ifndef ARA_PHM_RECOVERY_ACTION_H_
#define ARA_PHM_RECOVERY_ACTION_H_

#include <string>

#include "ara/core/instance_specifier.h"
#include "ara/phm/health_monitor.h"

namespace ara::phm {

class RecoveryAction final {
public:
  RecoveryAction(ara::core::InstanceSpecifier const &instance, RecoveryHandler handler)
      : RecoveryAction(instance, std::move(handler), HealthMonitor::Instance()) {}
  /// Variant bound to an explicit monitor (used by tests and multi-monitor hosts).
  RecoveryAction(ara::core::InstanceSpecifier const &instance, RecoveryHandler handler,
                 HealthMonitor &monitor)
      : mName(instance.ToString()), mHandler(std::move(handler)), mMonitor(&monitor) {}

  RecoveryAction(RecoveryAction const &) = delete;
  RecoveryAction &operator=(RecoveryAction const &) = delete;
  ~RecoveryAction() { StopOffer(); }

  /// Starts receiving recovery requests (kAlreadyOffered if already offered).
  ara::core::Result<void> Offer();
  void StopOffer() noexcept;

private:
  std::string mName;
  RecoveryHandler mHandler;
  HealthMonitor *mMonitor;
  bool mOffered{false};
};

} // namespace ara::phm

#endif // ARA_PHM_RECOVERY_ACTION_H_
