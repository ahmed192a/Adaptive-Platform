/** @file network_manager.h  Registry of NM networks (one per bus / channel). */
#ifndef ARA_NM_NETWORK_MANAGER_H_
#define ARA_NM_NETWORK_MANAGER_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "ara/nm/nm_channel.h"

namespace ara::nm {

class NetworkManager {
public:
  explicit NetworkManager(std::shared_ptr<NmClock> clock = std::make_shared<SteadyNmClock>())
      : clock_(std::move(clock)) {}

  /// kNetworkExists for a duplicate name, kInvalidConfig for a bad config.
  ara::core::Result<NmChannel *> CreateNetwork(std::string const &name, NmConfig const &cfg);
  /// kUnknownNetwork if the name was never created.
  ara::core::Result<NmChannel *> GetNetwork(std::string const &name) const;
  /// Bind the port instance specifier (as used by ara::nm::NetworkHandle) to a created network.
  /// kUnknownNetwork if the network does not exist. The binding is process-wide.
  ara::core::Result<void> BindPort(std::string const &instanceSpecifier, std::string const &network);
  /// Remove a port binding made with BindPort (no-op if absent).
  static void UnbindPort(std::string const &instanceSpecifier);
  /// SWS_ANM_00090: withdraw every network and partial network request (SIGTERM / shutdown).
  void WithdrawAll();
  /// Process() every network.
  void ProcessAll();
  std::size_t Size() const;

private:
  std::shared_ptr<NmClock> clock_;
  mutable std::mutex mu_;
  std::map<std::string, std::shared_ptr<NmChannel>> nets_;
};

} // namespace ara::nm

#endif // ARA_NM_NETWORK_MANAGER_H_
