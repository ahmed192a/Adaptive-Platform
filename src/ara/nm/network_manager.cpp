#include "ara/nm/network_manager.h"

#include <vector>

#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

using ara::core::Result;

Result<NetworkHandle *> NetworkManager::CreateNetwork(std::string const &name, NmConfig const &cfg) {
  std::lock_guard<std::mutex> l(mu_);
  if (nets_.count(name) != 0) return Result<NetworkHandle *>::FromError(MakeErrorCode(NmErrc::kNetworkExists));
  auto h = NetworkHandle::Create(name, cfg, clock_);
  if (!h.HasValue()) return Result<NetworkHandle *>::FromError(h.Error());
  NetworkHandle *raw = h.Value().get();
  nets_.emplace(name, std::move(h).Value());
  return Result<NetworkHandle *>::FromValue(raw);
}

Result<NetworkHandle *> NetworkManager::GetNetwork(std::string const &name) const {
  std::lock_guard<std::mutex> l(mu_);
  auto it = nets_.find(name);
  if (it == nets_.end()) return Result<NetworkHandle *>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  return Result<NetworkHandle *>::FromValue(it->second.get());
}

void NetworkManager::ProcessAll() {
  std::vector<NetworkHandle *> all;
  {
    std::lock_guard<std::mutex> l(mu_);
    for (auto &kv : nets_) all.push_back(kv.second.get());
  }
  for (auto *h : all) h->Process();
}

std::size_t NetworkManager::Size() const {
  std::lock_guard<std::mutex> l(mu_);
  return nets_.size();
}

} // namespace ara::nm
