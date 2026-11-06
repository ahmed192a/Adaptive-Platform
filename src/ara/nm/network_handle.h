/**
 * @file network_handle.h
 * @brief ara::nm::NetworkHandle - the State Management facing API of Network Management
 *        (SWS_ANM_01000 ff., AUTOSAR AP R25-11).
 *
 * A handle is bound to a NetworkManagementPortInterface port by its InstanceSpecifier. In this
 * implementation the binding is made with NetworkManager::BindPort(); an unbound handle reports
 * kServiceNotAvailable like a handle whose NM daemon connection was lost.
 *
 * Deviations (see doc/spec-conformance/nm.md): the template overloads taking an ExecutorT are not
 * provided; notifiers run in the thread that drives NmChannel::Process()/Request()/....
 */
#ifndef ARA_NM_NETWORK_HANDLE_H_
#define ARA_NM_NETWORK_HANDLE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"
#include "ara/nm/nm_channel.h"

namespace ara::nm {

/// SWS_ANM_01358
enum class NetworkStateType : std::uint32_t { kNoCom = 0, kFullCom = 1 };

namespace internal {
/// Process-wide port instance specifier -> NM channel binding.
class PortRegistry {
public:
  static void Bind(std::string const &specifier, std::shared_ptr<NmChannel> const &channel);
  static void Unbind(std::string const &specifier);
  /// All live channels bound to the port (several channels are aggregated, SWS_ANM_00083/00084).
  static std::vector<std::shared_ptr<NmChannel>> LookupAll(std::string const &specifier);
};
} // namespace internal

class NetworkHandle final {
public:
  using NetworkStateChangeNotifier = std::function<void(NetworkStateType const &)>; // SWS_ANM_01020

  explicit NetworkHandle(ara::core::InstanceSpecifier const &specifier) noexcept; // SWS_ANM_01001
  NetworkHandle(NetworkHandle const &) = delete;                                  // SWS_ANM_01005
  NetworkHandle &operator=(NetworkHandle const &) = delete;                       // SWS_ANM_01006
  NetworkHandle(NetworkHandle &&other) noexcept;                                  // SWS_ANM_01003
  NetworkHandle &operator=(NetworkHandle &&other) & noexcept;                     // SWS_ANM_01004
  ~NetworkHandle() noexcept;                                                      // SWS_ANM_01002

  /// kFullCom iff the network is in Network Mode (RepeatMessage/NormalOperation/ReadySleep),
  /// kNoCom in Bus-Sleep / Prepare Bus-Sleep (SWS_ANM_00029, 00083).
  ara::core::Result<NetworkStateType> GetNetworkState() const noexcept;          // SWS_ANM_01007
  ara::core::Result<NetworkStateType> GetNetworkRequestedState() const noexcept; // SWS_ANM_01008
  /// kFullCom requests the network, kNoCom releases it (SWS_ANM_00084). Idempotent.
  ara::core::Result<void> SetNetworkRequestedState(NetworkStateType state) noexcept; // SWS_ANM_01009

  /// One notifier per kind; registering again overwrites. Empty notifier -> kInvalidHandler.
  ara::core::Result<void> RegisterNetworkStateChangeNotifier(NetworkStateChangeNotifier notifier) noexcept;          // 01010
  ara::core::Result<void> RegisterNetworkRequestedStateChangeNotifier(NetworkStateChangeNotifier notifier) noexcept; // 01012
  /// Overloads with an executor (SWS_ANM_01014, 01015). ExecutorT is any copy/move-constructible
  /// callable that can be invoked with a std::function<void()> and runs it (immediately or later,
  /// in any thread): `executor(std::function<void()>{...})`. The notifier is delivered through it.
  template <typename ExecutorT>
  ara::core::Result<void> RegisterNetworkStateChangeNotifier(NetworkStateChangeNotifier notifier,
                                                              ExecutorT &&executor) noexcept {
    return RegisterNetworkStateChangeNotifier(Wrap(std::move(notifier), std::forward<ExecutorT>(executor)));
  }
  template <typename ExecutorT>
  ara::core::Result<void> RegisterNetworkRequestedStateChangeNotifier(NetworkStateChangeNotifier notifier,
                                                                       ExecutorT &&executor) noexcept {
    return RegisterNetworkRequestedStateChangeNotifier(
        Wrap(std::move(notifier), std::forward<ExecutorT>(executor)));
  }

  void UnregisterNetworkStateChangeNotifier() noexcept;          // SWS_ANM_01011
  void UnregisterNetworkRequestedStateChangeNotifier() noexcept; // SWS_ANM_01013

private:
  template <typename ExecutorT>
  static NetworkStateChangeNotifier Wrap(NetworkStateChangeNotifier notifier, ExecutorT &&executor) {
    if (!notifier) return notifier; // rejected with kInvalidHandler by the plain overload
    auto ex = std::make_shared<std::decay_t<ExecutorT>>(std::forward<ExecutorT>(executor));
    return [notifier = std::move(notifier), ex](NetworkStateType const &state) {
      NetworkStateType const s = state;
      (*ex)(std::function<void()>([notifier, s] { notifier(s); }));
    };
  }
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace ara::nm

#endif // ARA_NM_NETWORK_HANDLE_H_
