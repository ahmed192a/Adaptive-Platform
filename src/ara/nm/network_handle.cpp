#include "ara/nm/network_handle.h"

#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

using ara::core::Result;

namespace internal {
namespace {
std::mutex gMu;
std::map<std::string, std::weak_ptr<NmChannel>> &Map() {
  static std::map<std::string, std::weak_ptr<NmChannel>> m;
  return m;
}
} // namespace

void PortRegistry::Bind(std::string const &specifier, std::shared_ptr<NmChannel> const &channel) {
  std::lock_guard<std::mutex> l(gMu);
  Map()[specifier] = channel;
}
void PortRegistry::Unbind(std::string const &specifier) {
  std::lock_guard<std::mutex> l(gMu);
  Map().erase(specifier);
}
std::shared_ptr<NmChannel> PortRegistry::Lookup(std::string const &specifier) {
  std::lock_guard<std::mutex> l(gMu);
  auto it = Map().find(specifier);
  return it == Map().end() ? nullptr : it->second.lock();
}
} // namespace internal

namespace {
NetworkStateType Map(NmState s) noexcept {
  return (s == NmState::kRepeatMessage || s == NmState::kNormalOperation || s == NmState::kReadySleep)
             ? NetworkStateType::kFullCom
             : NetworkStateType::kNoCom;
}
} // namespace

struct NetworkHandle::Impl {
  std::string specifier;
  std::mutex mu;
  NetworkStateChangeNotifier stateNotifier;
  NetworkStateChangeNotifier requestedNotifier;
  std::weak_ptr<NmChannel> observed; // channel the observers below are registered on
  std::size_t stateObs{0};
  std::size_t reqObs{0};
};

NetworkHandle::NetworkHandle(ara::core::InstanceSpecifier const &specifier) noexcept
    : impl_(std::make_shared<Impl>()) {
  impl_->specifier = std::string(specifier.ToString());
}

NetworkHandle::NetworkHandle(NetworkHandle &&other) noexcept = default;
NetworkHandle &NetworkHandle::operator=(NetworkHandle &&other) & noexcept = default;

NetworkHandle::~NetworkHandle() noexcept {
  if (!impl_) return;
  if (auto ch = impl_->observed.lock()) {
    if (impl_->stateObs != 0) ch->RemoveStateObserver(impl_->stateObs);
    if (impl_->reqObs != 0) ch->RemoveRequestedObserver(impl_->reqObs);
  }
}

Result<NetworkStateType> NetworkHandle::GetNetworkState() const noexcept {
  auto ch = internal::PortRegistry::Lookup(impl_->specifier);
  if (!ch) return Result<NetworkStateType>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  return Result<NetworkStateType>::FromValue(Map(ch->GetState()));
}

Result<NetworkStateType> NetworkHandle::GetNetworkRequestedState() const noexcept {
  auto ch = internal::PortRegistry::Lookup(impl_->specifier);
  if (!ch) return Result<NetworkStateType>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  return Result<NetworkStateType>::FromValue(ch->IsRequested() ? NetworkStateType::kFullCom
                                                                : NetworkStateType::kNoCom);
}

Result<void> NetworkHandle::SetNetworkRequestedState(NetworkStateType state) noexcept {
  auto ch = internal::PortRegistry::Lookup(impl_->specifier);
  if (!ch) return Result<void>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  if (state == NetworkStateType::kFullCom) return ch->Request();
  if (ch->IsRequested()) return ch->Release();
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::RegisterNetworkStateChangeNotifier(NetworkStateChangeNotifier notifier) noexcept {
  if (!notifier) return Result<void>::FromError(MakeErrorCode(NmErrc::kInvalidHandler));
  auto ch = internal::PortRegistry::Lookup(impl_->specifier);
  if (!ch) return Result<void>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  std::lock_guard<std::mutex> l(impl_->mu);
  impl_->stateNotifier = std::move(notifier);
  if (impl_->stateObs == 0) {
    std::weak_ptr<Impl> weak = impl_;
    // Observers run without any channel lock held; the weak_ptr makes a concurrent
    // ~NetworkHandle safe.
    impl_->stateObs = ch->AddStateObserver([weak](NmState from, NmState to) {
      if (Map(from) == Map(to)) return;
      auto impl = weak.lock();
      if (!impl) return;
      NetworkStateChangeNotifier n;
      {
        std::lock_guard<std::mutex> g(impl->mu);
        n = impl->stateNotifier;
      }
      if (n) n(Map(to));
    });
    impl_->observed = ch;
  }
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::RegisterNetworkRequestedStateChangeNotifier(
    NetworkStateChangeNotifier notifier) noexcept {
  if (!notifier) return Result<void>::FromError(MakeErrorCode(NmErrc::kInvalidHandler));
  auto ch = internal::PortRegistry::Lookup(impl_->specifier);
  if (!ch) return Result<void>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  std::lock_guard<std::mutex> l(impl_->mu);
  impl_->requestedNotifier = std::move(notifier);
  if (impl_->reqObs == 0) {
    std::weak_ptr<Impl> weak = impl_;
    impl_->reqObs = ch->AddRequestedObserver([weak](bool requested) {
      auto impl = weak.lock();
      if (!impl) return;
      NetworkStateChangeNotifier n;
      {
        std::lock_guard<std::mutex> g(impl->mu);
        n = impl->requestedNotifier;
      }
      if (n) n(requested ? NetworkStateType::kFullCom : NetworkStateType::kNoCom);
    });
    impl_->observed = ch;
  }
  return Result<void>::FromValue();
}

void NetworkHandle::UnregisterNetworkStateChangeNotifier() noexcept {
  std::lock_guard<std::mutex> l(impl_->mu);
  impl_->stateNotifier = nullptr;
}

void NetworkHandle::UnregisterNetworkRequestedStateChangeNotifier() noexcept {
  std::lock_guard<std::mutex> l(impl_->mu);
  impl_->requestedNotifier = nullptr;
}

} // namespace ara::nm
