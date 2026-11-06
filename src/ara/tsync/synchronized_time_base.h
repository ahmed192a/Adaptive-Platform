/**
 * @file synchronized_time_base.h
 * @brief SWS-facing Time Synchronization API (AUTOSAR AP R25-11): SynchronizedTimeBaseConsumer,
 *        SynchronizedTimeBaseProvider, SynchronizedTimeBaseStatus.
 *
 * The classes are bound to a time base by their InstanceSpecifier through TimeBaseRegistry
 * (TimeBaseRegistry::Bind). An unbound consumer/provider behaves like one whose time-sync daemon
 * connection was lost. Signatures that the retrievable SWS text does not give (callback types,
 * Register* return types, user-data type) are marked "unverified" in
 * doc/spec-conformance/tsync.md. ara::core::Span is not used (it does not compile under -Werror),
 * user data is passed as pointer + length / returned as vector.
 */
#ifndef ARA_TSYNC_SYNCHRONIZED_TIME_BASE_H_
#define ARA_TSYNC_SYNCHRONIZED_TIME_BASE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"
#include "ara/tsync/time_base.h"

namespace ara::tsync {

namespace internal {
/// Validation-notification registration held by a provider (one notifier, a new one overwrites).
struct ValidationRegistration;
} // namespace internal

/// Process-wide InstanceSpecifier -> time base binding.
class TimeBaseRegistry {
public:
  static void Bind(std::string const &specifier, std::shared_ptr<TimeBaseResource> resource);
  static void Unbind(std::string const &specifier);
  static std::shared_ptr<TimeBaseResource> Lookup(std::string const &specifier);
};

/// Immutable sample of a time base: time, status, leap and user data (SWS 8.6.2).
class SynchronizedTimeBaseStatus {
public:
  SynchronizedTimeBaseStatus() = default;
  explicit SynchronizedTimeBaseStatus(TimeBaseSnapshot const &snap, bool synchronizedEver)
      : snap_(snap), ever_(synchronizedEver) {}

  SynchronizationStatus GetSynchronizationStatus() const noexcept {
    return ever_ ? ToSynchronizationStatus(snap_.status) : SynchronizationStatus::kNotSynchronizedUntilStartup;
  }
  LeapJump GetLeapJump() const noexcept { return snap_.leapJump; }
  /// Empty if the provider supplied no user data (SWS_TS_00120).
  std::vector<std::uint8_t> GetUserData() const {
    return std::vector<std::uint8_t>(snap_.userData.bytes, snap_.userData.bytes + snap_.userData.length);
  }
  Timestamp GetCreationTime() const noexcept { return snap_.time; }
  std::chrono::steady_clock::time_point GetCreationLocalTime() const noexcept { return snap_.localTime; }
  /// RATE_CORRECTED: a valid rate correction has been calculated (SWS_TS_14224).
  bool GetRateCorrected() const noexcept { return snap_.rateCorrected; }
  /// RATE_EXCEEDED: |rate deviation| exceeds rateCorrectionThreshold; 0 disables (SWS_TS_14228).
  bool GetRateExceeded() const noexcept { return snap_.rateExceeded; }

private:
  TimeBaseSnapshot snap_{};
  bool ever_{false};
};

class SynchronizedTimeBaseConsumer final {
public:
  using StatusChangeNotifier = std::function<void(SynchronizedTimeBaseStatus const &)>;           // unverified
  using SynchronizationStateChangeNotifier = std::function<void(SynchronizationStatus)>;          // unverified
  using TimeLeapNotifier = std::function<void(LeapJump)>;                                         // unverified
  using PrecisionMeasurementNotifier = std::function<void(TimePrecisionMeasurement const &)>;     // 00800, unverified
  using TimeValidationNotifier = std::function<void(TimeValidationData const &)>;                 // 01016, unverified

  explicit SynchronizedTimeBaseConsumer(ara::core::InstanceSpecifier const &specifier) noexcept;
  SynchronizedTimeBaseConsumer(SynchronizedTimeBaseConsumer const &) = delete;
  SynchronizedTimeBaseConsumer &operator=(SynchronizedTimeBaseConsumer const &) = delete;
  SynchronizedTimeBaseConsumer(SynchronizedTimeBaseConsumer &&) noexcept;
  SynchronizedTimeBaseConsumer &operator=(SynchronizedTimeBaseConsumer &&) & noexcept;
  ~SynchronizedTimeBaseConsumer() noexcept;

  /// SWS_TS_14218: samples time + status atomically. Unbound/never synchronized ->
  /// kNotSynchronizedUntilStartup.
  SynchronizedTimeBaseStatus GetTimeWithStatus() const noexcept;
  /// SWS_TS_00202 / 00070: 0.0 if no rate deviation has been calculated.
  double GetRateDeviation() const noexcept;

  /// One notifier per kind, a new registration overwrites. kDaemonConnectionLost if unbound,
  /// kInvalidArgument for an empty notifier. (Return type unverified.)
  ara::core::Result<void> RegisterStatusChangeNotifier(StatusChangeNotifier n) noexcept;                  // 00701
  ara::core::Result<void> RegisterSynchronizationStateChangeNotifier(SynchronizationStateChangeNotifier n) noexcept; // 00702
  ara::core::Result<void> RegisterTimeLeapNotifier(TimeLeapNotifier n) noexcept;                          // 00703
  /// SWS_TS_00800/00803: kFunctionNotSupported for a system-wide global time master base,
  /// kDaemonConnectionLost if unbound, kInvalidArgument for an empty notifier.
  ara::core::Result<void> RegisterTimePrecisionMeasurementNotifier(PrecisionMeasurementNotifier n) noexcept;
  /// SWS_TS_01016: receive master timing / pdelay responder data.
  ara::core::Result<void> RegisterTimeValidationNotification(TimeValidationNotifier n) noexcept;
  void UnregisterStatusChangeNotifier() noexcept;
  void UnregisterSynchronizationStateChangeNotifier() noexcept;
  void UnregisterTimeLeapNotifier() noexcept;
  void UnregisterTimePrecisionMeasurementNotifier() noexcept;
  void UnregisterTimeValidationNotification() noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

class SynchronizedTimeBaseProvider final {
public:
  explicit SynchronizedTimeBaseProvider(ara::core::InstanceSpecifier const &specifier) noexcept;
  SynchronizedTimeBaseProvider(SynchronizedTimeBaseProvider const &) = delete;
  SynchronizedTimeBaseProvider &operator=(SynchronizedTimeBaseProvider const &) = delete;
  SynchronizedTimeBaseProvider(SynchronizedTimeBaseProvider &&) noexcept = default;
  SynchronizedTimeBaseProvider &operator=(SynchronizedTimeBaseProvider &&) & noexcept = default;
  ~SynchronizedTimeBaseProvider() noexcept = default;

  using TimeValidationNotifier = SynchronizedTimeBaseConsumer::TimeValidationNotifier;
  /// SWS_TS_01114/01115: one validation notifier, a new registration overwrites the previous one.
  ara::core::Result<void> RegisterTimeValidationNotification(TimeValidationNotifier n) noexcept;
  void UnregisterTimeValidationNotification() noexcept;
  /// SWS_TS_00425/00427: hand master timing / pdelay responder data to the validation notifications.
  ara::core::Result<void> SetMasterTimingData(TimeValidationData data) noexcept;
  ara::core::Result<void> SetPdelayResponderData(TimeValidationData data) noexcept;

  /// Sets the time unconditionally (no slewing). kDaemonConnectionLost if unbound,
  /// kInvalidArgument for more than 3 user data bytes.
  ara::core::Result<void> SetTime(Timestamp t, std::uint8_t const *userData = nullptr, std::size_t userDataSize = 0) noexcept;
  /// Updates the time; small offsets are slewed in, large ones step the time.
  ara::core::Result<void> UpdateTime(Timestamp t, std::uint8_t const *userData = nullptr, std::size_t userDataSize = 0) noexcept;
  /// Current time of the base (epoch if it was never set).
  Timestamp GetCurrentTime() const noexcept;
  /// SWS_TS_00061..63: kFunctionNotSupported unless allowProviderRateCorrection, kLimitsExceeded
  /// beyond providerRateDeviationMax.
  ara::core::Result<void> SetRateCorrection(double rateCorrection) noexcept;
  double GetRateDeviation() const noexcept;
  ara::core::Result<void> SetUserData(std::uint8_t const *userData, std::size_t userDataSize) noexcept;
  std::vector<std::uint8_t> GetUserData() const;

private:
  std::string specifier_;
  std::shared_ptr<internal::ValidationRegistration> validation_;
};

/**
 * Provider of an offset time base (SWS_TS_01201..01214, R22-11): the time is set by the application
 * with SetOffsetTime. The bound time base must be configured with isOffsetTimeBase.
 */
class OffsetTimeBaseProvider final {
public:
  using TimeValidationNotifier = SynchronizedTimeBaseConsumer::TimeValidationNotifier;
  explicit OffsetTimeBaseProvider(ara::core::InstanceSpecifier const &specifier) noexcept; // 01201
  OffsetTimeBaseProvider(OffsetTimeBaseProvider const &) = delete;
  OffsetTimeBaseProvider &operator=(OffsetTimeBaseProvider const &) = delete;                // 01205
  OffsetTimeBaseProvider(OffsetTimeBaseProvider &&) noexcept = default;                      // 01202
  OffsetTimeBaseProvider &operator=(OffsetTimeBaseProvider &&) & noexcept = default;         // 01203
  ~OffsetTimeBaseProvider() noexcept = default;                                              // 01206

  /// SWS_TS_01207: sets a new offset time (unconditional step). kTimeCannotSet if the bound time
  /// base is not an offset time base, kDaemonConnectionLost if unbound, kInvalidArgument for more
  /// than 3 user data bytes.
  ara::core::Result<void> SetOffsetTime(Timestamp t, std::uint8_t const *userData = nullptr,
                                        std::size_t userDataSize = 0) noexcept;
  Timestamp GetCurrentTime() const noexcept;                                                 // 01208
  ara::core::Result<void> SetRateCorrection(double rateCorrection) noexcept;                 // 01209
  double GetRateDeviation() const noexcept;                                                  // 01210
  ara::core::Result<void> SetUserData(std::uint8_t const *userData, std::size_t userDataSize) noexcept; // 01211
  std::vector<std::uint8_t> GetUserData() const;                                             // 01212
  ara::core::Result<void> RegisterTimeValidationNotification(TimeValidationNotifier n) noexcept; // 01213
  void UnregisterTimeValidationNotification() noexcept;                                      // 01214
  ara::core::Result<void> SetMasterTimingData(TimeValidationData data) noexcept;
  ara::core::Result<void> SetPdelayResponderData(TimeValidationData data) noexcept;

private:
  std::string specifier_;
  std::shared_ptr<internal::ValidationRegistration> validation_;
};

} // namespace ara::tsync

#endif // ARA_TSYNC_SYNCHRONIZED_TIME_BASE_H_
