#include "ara/nm/network_manager.h"

#include <vector>

#include "ara/nm/network_handle.h"
#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

using ara::core::Result;

Result<NmChannel *> NetworkManager::CreateNetwork(std::string const &name, NmConfig const &cfg) {
  std::lock_guard<std::mutex> l(mu_);
  if (nets_.count(name) != 0) return Result<NmChannel *>::FromError(MakeErrorCode(NmErrc::kNetworkExists));
  auto h = NmChannel::Create(name, cfg, clock_);
  if (!h.HasValue()) return Result<NmChannel *>::FromError(h.Error());
  NmChannel *raw = h.Value().get();
  nets_.emplace(name, std::shared_ptr<NmChannel>(std::move(h).Value()));
  return Result<NmChannel *>::FromValue(raw);
}

Result<NmChannel *> NetworkManager::GetNetwork(std::string const &name) const {
  std::lock_guard<std::mutex> l(mu_);
  auto it = nets_.find(name);
  if (it == nets_.end()) return Result<NmChannel *>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  return Result<NmChannel *>::FromValue(it->second.get());
}

Result<void> NetworkManager::BindPort(std::string const &instanceSpecifier, std::string const &network) {
  std::shared_ptr<NmChannel> ch;
  {
    std::lock_guard<std::mutex> l(mu_);
    auto it = nets_.find(network);
    if (it == nets_.end()) return Result<void>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
    ch = it->second;
  }
  internal::PortRegistry::Bind(instanceSpecifier, ch);
  return Result<void>::FromValue();
}

void NetworkManager::UnbindPort(std::string const &instanceSpecifier) {
  internal::PortRegistry::Unbind(instanceSpecifier);
}

void NetworkManager::WithdrawAll() {
  std::vector<std::shared_ptr<NmChannel>> all;
  {
    std::lock_guard<std::mutex> l(mu_);
    for (auto &kv : nets_) all.push_back(kv.second);
  }
  for (auto &c : all) c->Withdraw();
}

void NetworkManager::ProcessAll() {
  std::vector<NmChannel *> all;
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
