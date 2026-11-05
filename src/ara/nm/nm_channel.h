/**
 * @file nm_channel.h
 * @brief One NM-managed network: state machine, timers, partial networks.
 *
 * Time only moves when Process() (or any other public call) runs, so the
 * handle is deterministic with ManualClock. Callbacks are invoked without the
 * internal lock held and may call back into the handle.
 *
 * Transitions (simplified AUTOSAR NM):
 *   BusSleep/PrepareBusSleep --Request or NM msg--> RepeatMessage
 *   RepeatMessage --repeatMessageTime--> NormalOperation (requested) | ReadySleep
 *   NormalOperation --Release--> ReadySleep
 *   ReadySleep --Request--> NormalOperation
 *   NormalOperation/ReadySleep --msg with RMR--> RepeatMessage
 *   ReadySleep --nmTimeout without msgs--> PrepareBusSleep --waitBusSleepTime--> BusSleep
 */
#ifndef ARA_NM_NM_CHANNEL_H_
#define ARA_NM_NM_CHANNEL_H_

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ara/core/result.h"
#include "ara/nm/nm_pdu.h"
#include "ara/nm/nm_types.h"

namespace ara::nm {

class NmChannel {
public:
  using StateHandler = std::function<void(NmState from, NmState to)>;
  using TxHandler = std::function<void(NmPdu const &)>;
  using RequestedHandler = std::function<void(bool requested)>;

  /// Fails with kInvalidConfig when a timer is zero. Starts in BusSleep.
  static ara::core::Result<std::unique_ptr<NmChannel>>
  Create(std::string name, NmConfig cfg, std::shared_ptr<NmClock> clock);

  NmChannel(NmChannel const &) = delete;
  NmChannel &operator=(NmChannel const &) = delete;

  std::string const &Name() const noexcept { return name_; }
  NmState GetState() const;
  bool IsRequested() const;

  /// Network request. Idempotent.
  ara::core::Result<void> Request();
  /// Network release; kNotRequested if there was no Request().
  ara::core::Result<void> Release();

  /// Partial network (0..31) request / release. Independent of Request().
  ara::core::Result<void> RequestPartialNetwork(std::uint8_t pnId);
  ara::core::Result<void> ReleasePartialNetwork(std::uint8_t pnId);
  /// True if requested locally or by a remote node (and not yet expired).
  ara::core::Result<bool> IsPartialNetworkRequested(std::uint8_t pnId);

  /// Feed a received NM message (already decoded).
  void OnPduReceived(NmPdu const &pdu);
  /// Feed raw bytes; kBadPdu on a malformed message.
  ara::core::Result<void> OnBytesReceived(std::uint8_t const *bytes, std::size_t size);

  /// Advance timers to the clock's current time and transmit due messages.
  void Process();

  void SetStateChangeHandler(StateHandler h);
  void SetTxHandler(TxHandler h);

  /// Additional observers (used by the SWS facade ara::nm::NetworkHandle). Returns an id.
  std::size_t AddStateObserver(StateHandler h);
  void RemoveStateObserver(std::size_t id);
  std::size_t AddRequestedObserver(RequestedHandler h);
  void RemoveRequestedObserver(std::size_t id);

  /// SWS_ANM_00090: withdraw the network request and all local partial network requests
  /// (shutdown / SIGTERM). Never fails.
  void Withdraw();

private:
  NmChannel(std::string name, NmConfig cfg, std::shared_ptr<NmClock> clock);

  struct Pending {
    std::vector<std::pair<NmState, NmState>> changes;
    std::vector<NmPdu> tx;
    std::vector<bool> requestedChanges;
  };

  void Enter(NmState s, Millis t, Pending &p);
  void Pump(Millis now, Pending &p);
  bool Step(Millis now, Pending &p);
  void Dispatch(Pending &p);
  NmPdu MakePdu() const;

  std::string const name_;
  NmConfig const cfg_;
  std::shared_ptr<NmClock> const clock_;

  mutable std::mutex mu_;
  NmState state_{NmState::kBusSleep};
  bool requested_{false};
  std::uint32_t localPn_{0};
  std::uint32_t remotePn_{0};
  std::array<Millis, kMaxPartialNetworks> remotePnExpiry_{};
  Millis repeatDeadline_{0};
  Millis nmTimeoutDeadline_{0};
  Millis sleepDeadline_{0};
  Millis txDeadline_{0};
  StateHandler onState_;
  TxHandler onTx_;
  std::vector<std::pair<std::size_t, StateHandler>> stateObservers_;
  std::vector<std::pair<std::size_t, RequestedHandler>> requestedObservers_;
  std::size_t nextObserverId_{1};
  std::uint8_t immediateRemaining_{0};
};

} // namespace ara::nm

#endif // ARA_NM_NM_CHANNEL_H_
