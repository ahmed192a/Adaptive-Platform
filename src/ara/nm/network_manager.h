/** @file network_manager.h  Registry of NM networks (one per bus / channel). */
#ifndef ARA_NM_NETWORK_MANAGER_H_
#define ARA_NM_NETWORK_MANAGER_H_

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "ara/nm/network_handle.h"

namespace ara::nm {

class NetworkManager {
public:
  explicit NetworkManager(std::shared_ptr<NmClock> clock = std::make_shared<SteadyNmClock>())
      : clock_(std::move(clock)) {}

  /// kNetworkExists for a duplicate name, kInvalidConfig for a bad config.
  ara::core::Result<NetworkHandle *> CreateNetwork(std::string const &name, NmConfig const &cfg);
  /// kUnknownNetwork if the name was never created.
  ara::core::Result<NetworkHandle *> GetNetwork(std::string const &name) const;
  /// Process() every network.
  void ProcessAll();
  std::size_t Size() const;

private:
  std::shared_ptr<NmClock> clock_;
  mutable std::mutex mu_;
  std::map<std::string, std::unique_ptr<NetworkHandle>> nets_;
};

} // namespace ara::nm

#endif // ARA_NM_NETWORK_MANAGER_H_
