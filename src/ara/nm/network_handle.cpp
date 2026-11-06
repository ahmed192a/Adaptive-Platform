#include "ara/nm/network_handle.h"

#include <algorithm>
#include <iterator>
#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

using ara::core::Result;

namespace internal {
namespace {
std::mutex gMu;
std::map<std::string, std::vector<std::weak_ptr<NmChannel>>> &Map() {
  static std::map<std::string, std::vector<std::weak_ptr<NmChannel>>> m;
  return m;
}
} // namespace

void PortRegistry::Bind(std::string const &specifier, std::shared_ptr<NmChannel> const &channel) {
  std::lock_guard<std::mutex> l(gMu);
  auto &v = Map()[specifier];
  v.erase(std::remove_if(v.begin(), v.end(), [](auto const &w) { return w.expired(); }), v.end());
  for (auto const &w : v) {
    if (w.lock() == channel) return; // already bound
  }
  v.push_back(channel);
}

void PortRegistry::Unbind(std::string const &specifier) {
  std::lock_guard<std::mutex> l(gMu);
  Map().erase(specifier);
}

std::vector<std::shared_ptr<NmChannel>> PortRegistry::LookupAll(std::string const &specifier) {
  std::lock_guard<std::mutex> l(gMu);
  std::vector<std::shared_ptr<NmChannel>> out;
  auto it = Map().find(specifier);
  if (it == Map().end()) return out;
  for (auto const &w : it->second) {
    if (auto c = w.lock()) out.push_back(std::move(c));
  }
  return out;
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
  struct Obs {
    std::weak_ptr<NmChannel> channel;
    std::size_t stateObs{0};
    std::size_t reqObs{0};
  };
  std::string specifier;
  std::mutex mu;
  NetworkStateChangeNotifier stateNotifier;
  NetworkStateChangeNotifier requestedNotifier;
  std::vector<Obs> observed;                // one entry per channel an observer is registered on
  NetworkStateType lastState{NetworkStateType::kNoCom};
  NetworkStateType lastRequested{NetworkStateType::kNoCom};
};

namespace {
// SWS_ANM_00083: the aggregated network state is the lowest one (kNoCom if any channel is in No Com).
NetworkStateType AggregateState(std::vector<std::shared_ptr<NmChannel>> const &chs) {
  for (auto const &c : chs) {
    if (Map(c->GetState()) == NetworkStateType::kNoCom) return NetworkStateType::kNoCom;
  }
  return NetworkStateType::kFullCom;
}
// Aggregated requested state: the highest one (kFullCom if any channel is requested).
NetworkStateType AggregateRequested(std::vector<std::shared_ptr<NmChannel>> const &chs) {
  for (auto const &c : chs) {
    if (c->IsRequested()) return NetworkStateType::kFullCom;
  }
  return NetworkStateType::kNoCom;
}
} // namespace

NetworkHandle::NetworkHandle(ara::core::InstanceSpecifier const &specifier) noexcept
    : impl_(std::make_shared<Impl>()) {
  impl_->specifier = std::string(specifier.ToString());
}
NetworkHandle::NetworkHandle(NetworkHandle &&other) noexcept = default;
NetworkHandle &NetworkHandle::operator=(NetworkHandle &&other) & noexcept = default;

NetworkHandle::~NetworkHandle() noexcept {
  if (!impl_) return;
  for (auto &o : impl_->observed) {
    if (auto ch = o.channel.lock()) {
      if (o.stateObs != 0) ch->RemoveStateObserver(o.stateObs);
      if (o.reqObs != 0) ch->RemoveRequestedObserver(o.reqObs);
    }
  }
}

Result<NetworkStateType> NetworkHandle::GetNetworkState() const noexcept {
  auto chs = internal::PortRegistry::LookupAll(impl_->specifier);
  if (chs.empty()) return Result<NetworkStateType>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  return Result<NetworkStateType>::FromValue(AggregateState(chs));
}

Result<NetworkStateType> NetworkHandle::GetNetworkRequestedState() const noexcept {
  auto chs = internal::PortRegistry::LookupAll(impl_->specifier);
  if (chs.empty()) return Result<NetworkStateType>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  return Result<NetworkStateType>::FromValue(AggregateRequested(chs));
}

Result<void> NetworkHandle::SetNetworkRequestedState(NetworkStateType state) noexcept {
  auto chs = internal::PortRegistry::LookupAll(impl_->specifier);
  if (chs.empty()) return Result<void>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  // SWS_ANM_00084: the request applies to every bound channel.
  for (auto const &ch : chs) {
    Result<void> r = Result<void>::FromValue();
    if (state == NetworkStateType::kFullCom) r = ch->Request();
    else if (ch->IsRequested()) r = ch->Release();
    if (!r.HasValue()) return r;
  }
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::RegisterNetworkStateChangeNotifier(NetworkStateChangeNotifier notifier) noexcept {
  if (!notifier) return Result<void>::FromError(MakeErrorCode(NmErrc::kInvalidHandler));
  auto chs = internal::PortRegistry::LookupAll(impl_->specifier);
  if (chs.empty()) return Result<void>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  std::lock_guard<std::mutex> l(impl_->mu);
  impl_->stateNotifier = std::move(notifier);
  impl_->lastState = AggregateState(chs);
  for (auto const &ch : chs) {
    auto it = std::find_if(impl_->observed.begin(), impl_->observed.end(),
                           [&](Impl::Obs const &o) { return o.channel.lock() == ch; });
    if (it == impl_->observed.end()) {
      impl_->observed.push_back({ch, 0, 0});
      it = std::prev(impl_->observed.end());
    }
    if (it->stateObs != 0) continue;
    std::weak_ptr<Impl> weak = impl_;
    // Observers run without any channel lock held; the weak_ptr makes a concurrent
    // ~NetworkHandle safe. The notifier fires when the aggregated state changes.
    it->stateObs = ch->AddStateObserver([weak, spec = impl_->specifier](NmState from, NmState to) {
      if (Map(from) == Map(to)) return;
      auto impl = weak.lock();
      if (!impl) return;
      auto all = internal::PortRegistry::LookupAll(spec);
      NetworkStateType const agg = AggregateState(all);
      NetworkStateChangeNotifier n;
      {
        std::lock_guard<std::mutex> g(impl->mu);
        if (agg == impl->lastState) return;
        impl->lastState = agg;
        n = impl->stateNotifier;
      }
      if (n) n(agg);
    });
  }
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::RegisterNetworkRequestedStateChangeNotifier(
    NetworkStateChangeNotifier notifier) noexcept {
  if (!notifier) return Result<void>::FromError(MakeErrorCode(NmErrc::kInvalidHandler));
  auto chs = internal::PortRegistry::LookupAll(impl_->specifier);
  if (chs.empty()) return Result<void>::FromError(MakeErrorCode(NmErrc::kServiceNotAvailable));
  std::lock_guard<std::mutex> l(impl_->mu);
  impl_->requestedNotifier = std::move(notifier);
  impl_->lastRequested = AggregateRequested(chs);
  for (auto const &ch : chs) {
    auto it = std::find_if(impl_->observed.begin(), impl_->observed.end(),
                           [&](Impl::Obs const &o) { return o.channel.lock() == ch; });
    if (it == impl_->observed.end()) {
      impl_->observed.push_back({ch, 0, 0});
      it = std::prev(impl_->observed.end());
    }
    if (it->reqObs != 0) continue;
    std::weak_ptr<Impl> weak = impl_;
    it->reqObs = ch->AddRequestedObserver([weak, spec = impl_->specifier](bool) {
      auto impl = weak.lock();
      if (!impl) return;
      NetworkStateType const agg = AggregateRequested(internal::PortRegistry::LookupAll(spec));
      NetworkStateChangeNotifier n;
      {
        std::lock_guard<std::mutex> g(impl->mu);
        if (agg == impl->lastRequested) return;
        impl->lastRequested = agg;
        n = impl->requestedNotifier;
      }
      if (n) n(agg);
    });
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
