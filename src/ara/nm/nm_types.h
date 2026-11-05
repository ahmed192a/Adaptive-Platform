/**
 * @file nm_types.h
 * @brief NM states, configuration and the injectable clock.
 */
#ifndef ARA_NM_NM_TYPES_H_
#define ARA_NM_NM_TYPES_H_

#include <atomic>
#include <chrono>
#include <cstdint>

namespace ara::nm {

using Millis = std::chrono::milliseconds;

/// Network management states (AUTOSAR NM state machine, flattened).
enum class NmState : std::uint8_t {
  kBusSleep,
  kPrepareBusSleep,
  kReadySleep,
  kNormalOperation,
  kRepeatMessage,
};

char const *ToString(NmState s) noexcept;

/// Time source. Tests use ManualClock, production code SteadyNmClock.
class NmClock {
public:
  virtual ~NmClock() = default;
  virtual Millis Now() const noexcept = 0;
};

class SteadyNmClock final : public NmClock {
public:
  Millis Now() const noexcept override {
    return std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now().time_since_epoch());
  }
};

/// Deterministic clock advanced by hand.
class ManualClock final : public NmClock {
public:
  Millis Now() const noexcept override { return Millis(ms_.load()); }
  void Advance(Millis d) noexcept { ms_.fetch_add(d.count()); }
private:
  std::atomic<Millis::rep> ms_{0};
};

/// Per-network configuration. All times in milliseconds, none may be zero.
struct NmConfig {
  std::uint8_t nodeId{0};
  Millis repeatMessageTime{1500};   ///< minimum time spent in RepeatMessage
  Millis nmTimeout{2000};           ///< silence on the bus before ReadySleep -> PrepareBusSleep
  Millis waitBusSleepTime{1500};    ///< PrepareBusSleep -> BusSleep
  Millis messageCycleTime{500};     ///< NM message period in RepeatMessage / NormalOperation
  Millis pnResetTime{3000};         ///< remote partial network request expires after this
  std::uint32_t pnFilterMask{0};    ///< partial networks this node is a member of (bit n = PN n)
  /// nmImmediateNmTransmissions: extra NM messages sent at nmImmediateNmCycleTime when
  /// RepeatMessage is entered (0 disables, SWS_ANM Sec. 7.6.1).
  std::uint8_t immediateNmTransmissions{0};
  Millis immediateNmCycleTime{20};  ///< nmImmediateNmCycleTime
  /// allNmMessagesKeepAwake: with partial networking active (pnFilterMask != 0) an NM message
  /// carrying PNI=1 but no bit of a PN this node serves is dropped unless this is true
  /// (SWS_ANM_00081 / 00089).
  bool allNmMessagesKeepAwake{false};
};

constexpr std::uint8_t kMaxPartialNetworks = 32;

} // namespace ara::nm

#endif // ARA_NM_NM_TYPES_H_
