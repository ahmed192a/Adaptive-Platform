/**
 * @file watchdog_interface.h
 * @brief apext::phm::WatchdogInterface (SWS PHM, Appendix C)
 *
 * Abstraction of the platform specific hardware/software watchdog.  PHM calls
 * AliveNotification() each evaluation while the platform is healthy and
 * FireWatchdogReaction() when a supervision failure cannot be recovered.
 */
#ifndef ARA_PHM_WATCHDOG_INTERFACE_H_
#define ARA_PHM_WATCHDOG_INTERFACE_H_

namespace apext::phm {

class WatchdogInterface {
public:
  virtual ~WatchdogInterface() = default;
  /// Kick the watchdog ("I am alive").
  virtual void AliveNotification() = 0;
  /// Trigger the reaction of the watchdog (typically a reset).
  virtual void FireWatchdogReaction() = 0;
};

} // namespace apext::phm

#endif // ARA_PHM_WATCHDOG_INTERFACE_H_
