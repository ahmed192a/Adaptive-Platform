/**
 * @file time_base.h
 * @brief Synchronized time base: consumer view (TimeBaseResource) and provider (TimeBaseProvider).
 *
 * A time base is a continuously running synchronized time derived from the local steady clock:
 *
 *   T(now) = T_sync + (now - local_at_sync) * (1 + rateDeviation) + slewed offset correction
 *
 * The local clock is injectable so behaviour is deterministic in tests. Provider updates that
 * differ from the current time base by less than `stepThreshold` are slewed in over `slewWindow`,
 * larger ones step the time (and raise a notification). If no update arrives within `timeout`
 * the TIMEOUT flag is raised and SYNCHRONIZED cleared until the next update.
 */
#ifndef ARA_TSYNC_TIME_BASE_H_
#define ARA_TSYNC_TIME_BASE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "ara/core/result.h"
#include "ara/tsync/tsync_error_domain.h"

namespace ara::tsync {

using Duration = std::chrono::nanoseconds;
using LocalClock = std::function<std::chrono::steady_clock::time_point()>;
/// Synchronized (global) time, expressed as nanoseconds since the time base epoch.
using SynchronizedTimePoint = std::chrono::time_point<std::chrono::system_clock, Duration>;

/// Bit mask values of SynchronizedTimeBaseStatus.
enum class StatusFlag : std::uint32_t {
  kSynchronized = 0x01,   ///< time base was synchronized and is within its timeout
  kTimeout = 0x02,        ///< no update within the configured timeout
  kSyncToGateway = 0x04,  ///< time is synchronized to a gateway, not to the global master
  kTimeLeap = 0x08,       ///< last update stepped the time by more than the step threshold
  kPendingRateCorr = 0x10 ///< an offset is still being slewed in
};

class StatusFlags {
public:
  constexpr StatusFlags() noexcept = default;
  constexpr StatusFlags(StatusFlag f) noexcept : bits_(static_cast<std::uint32_t>(f)) {}
  constexpr explicit StatusFlags(std::uint32_t bits) noexcept : bits_(bits) {}
  constexpr bool Test(StatusFlag f) const noexcept { return (bits_ & static_cast<std::uint32_t>(f)) != 0; }
  constexpr std::uint32_t Bits() const noexcept { return bits_; }
  constexpr StatusFlags operator|(StatusFlags o) const noexcept { return StatusFlags(bits_ | o.bits_); }
  constexpr bool operator==(StatusFlags const &o) const noexcept { return bits_ == o.bits_; }
  constexpr bool operator!=(StatusFlags const &o) const noexcept { return bits_ != o.bits_; }
private:
  std::uint32_t bits_{0};
};
constexpr StatusFlags operator|(StatusFlag a, StatusFlag b) noexcept { return StatusFlags(a) | StatusFlags(b); }

/// Optional user data transported with a time base update (SWS: UserData, up to 3 bytes).
struct UserData {
  std::uint8_t length{0};
  std::uint8_t bytes[3]{0, 0, 0};
};

/// One consistent reading of the time base.
struct TimeBaseSnapshot {
  SynchronizedTimePoint time{};
  double rateDeviation{0.0}; ///< dimensionless, +1e-6 == local clock runs 1 ppm fast
  StatusFlags status{};
  UserData userData{};
};

struct TimeBaseConfig {
  std::string name{"default"};
  LocalClock clock{};                                ///< default: std::chrono::steady_clock
  Duration timeout{std::chrono::seconds(1)};         ///< max age of the last update
  Duration stepThreshold{std::chrono::milliseconds(10)}; ///< larger offsets step instead of slew
  Duration slewWindow{std::chrono::milliseconds(500)};   ///< time over which a small offset is applied
};

/// Notification reasons passed to the update callback.
enum class ChangeReason { kStatus, kTimeStep, kUpdate };

class TimeBaseProvider;

/**
 * @brief Consumer view of a synchronized time base (thread-safe).
 */
class TimeBaseResource {
public:
  using UpdateCallback = std::function<void(TimeBaseSnapshot const &, ChangeReason)>;

  static ara::core::Result<std::shared_ptr<TimeBaseResource>> Create(TimeBaseConfig config);

  std::string const &Name() const noexcept { return config_.name; }

  /// Synchronized time now; kNotSynchronized if no update was ever received.
  /// After a timeout the extrapolated time is still returned (status carries TIMEOUT).
  ara::core::Result<TimeBaseSnapshot> GetCurrentTime() const;
  ara::core::Result<double> GetRateDeviation() const;
  StatusFlags GetStatus() const;
  UserData GetUserData() const;

  /// Register a callback invoked on status changes, time steps and (if `notifyEveryUpdate`) updates.
  void SetUpdateCallback(UpdateCallback cb, bool notifyEveryUpdate = false);

  /// Evaluates timeout detection and delivers pending notifications. Call periodically
  /// (getters evaluate lazily but cannot deliver callbacks from const readers).
  void Poll();

private:
  friend class TimeBaseProvider;
  explicit TimeBaseResource(TimeBaseConfig config);

  struct State {
    bool everSynced{false};
    std::int64_t baseNs{0};              ///< synchronized time at localAtSync (plus applied offset)
    std::chrono::steady_clock::time_point localAtSync{};
    std::chrono::steady_clock::time_point lastUpdate{};
    double rate{0.0};
    std::int64_t pendingOffsetNs{0};     ///< remaining offset to slew in (at localAtSync)
    std::chrono::steady_clock::time_point slewEnd{};
    bool gateway{false};
    bool leap{false};
    bool timeout{false};
    UserData user{};
  };

  std::chrono::steady_clock::time_point Now() const { return config_.clock(); }
  std::int64_t TimeAtLocked(State const &s, std::chrono::steady_clock::time_point now) const;
  StatusFlags StatusLocked(State const &s, std::chrono::steady_clock::time_point now) const;
  TimeBaseSnapshot SnapshotLocked(std::chrono::steady_clock::time_point now) const;

  // provider entry point
  ara::core::Result<void> Apply(SynchronizedTimePoint t, double rate, bool gateway, UserData user);

  void Deliver(TimeBaseSnapshot const &snap, ChangeReason reason, bool statusChanged);

  TimeBaseConfig config_;
  mutable std::mutex mutex_;
  State state_;
  StatusFlags lastNotified_{};
  UpdateCallback callback_;
  bool notifyEvery_{false};
};

/**
 * @brief Provider side: feeds synchronized times (e.g. from a gPTP/NTP/GNSS source) into a time base.
 */
class TimeBaseProvider {
public:
  explicit TimeBaseProvider(std::shared_ptr<TimeBaseResource> resource) noexcept
      : resource_(std::move(resource)) {}

  /// Provide a new synchronized time. `rateDeviation` of the local clock vs. the master.
  ara::core::Result<void> Update(SynchronizedTimePoint t, double rateDeviation = 0.0,
                                 bool syncToGateway = false, UserData userData = {}) {
    return resource_->Apply(t, rateDeviation, syncToGateway, userData);
  }

private:
  std::shared_ptr<TimeBaseResource> resource_;
};

} // namespace ara::tsync

#endif // ARA_TSYNC_TIME_BASE_H_
