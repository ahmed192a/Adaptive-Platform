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

private:
  TimeBaseSnapshot snap_{};
  bool ever_{false};
};

class SynchronizedTimeBaseConsumer final {
public:
  using StatusChangeNotifier = std::function<void(SynchronizedTimeBaseStatus const &)>;           // unverified
  using SynchronizationStateChangeNotifier = std::function<void(SynchronizationStatus)>;          // unverified
  using TimeLeapNotifier = std::function<void(LeapJump)>;                                         // unverified

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
  void UnregisterStatusChangeNotifier() noexcept;
  void UnregisterSynchronizationStateChangeNotifier() noexcept;
  void UnregisterTimeLeapNotifier() noexcept;

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
};

} // namespace ara::tsync

#endif // ARA_TSYNC_SYNCHRONIZED_TIME_BASE_H_
