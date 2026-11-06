#include "ara/nm/network_manager.h"

#include <csignal>
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

void NetworkManager::ShutdownAll() {
  std::vector<std::shared_ptr<NmChannel>> all;
  {
    std::lock_guard<std::mutex> l(mu_);
    for (auto &kv : nets_) all.push_back(kv.second);
  }
  for (auto &c : all) c->Shutdown();
}

namespace {
volatile std::sig_atomic_t gSigterm = 0;
struct sigaction gPrev {};
bool gInstalled = false;
std::mutex gHookMu;

extern "C" void NmSigtermHandler(int sig) {
  gSigterm = 1;
  // Chain to a previously installed function handler (not SIG_DFL / SIG_IGN).
  if ((gPrev.sa_flags & SA_SIGINFO) == 0 && gPrev.sa_handler != SIG_DFL && gPrev.sa_handler != SIG_IGN &&
      gPrev.sa_handler != nullptr) {
    gPrev.sa_handler(sig);
  }
}
} // namespace

Result<void> NetworkManager::InstallSigtermHook() {
  std::lock_guard<std::mutex> l(gHookMu);
  if (gInstalled) return Result<void>::FromValue();
  struct sigaction sa {};
  sa.sa_handler = NmSigtermHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGTERM, &sa, &gPrev) != 0) return Result<void>::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  gInstalled = true;
  return Result<void>::FromValue();
}

void NetworkManager::UninstallSigtermHook() {
  std::lock_guard<std::mutex> l(gHookMu);
  if (!gInstalled) return;
  sigaction(SIGTERM, &gPrev, nullptr);
  gInstalled = false;
  gSigterm = 0;
}

bool NetworkManager::SigtermPending() noexcept { return gSigterm != 0; }

bool NetworkManager::ServiceSignals() {
  if (gSigterm == 0) return false;
  gSigterm = 0;
  ShutdownAll();
  return true;
}

void NetworkManager::ProcessAll() {
  ServiceSignals();
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
