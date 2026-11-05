#include "ara/nm/network_handle.h"

#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

using ara::core::Result;

namespace {
bool Transmits(NmState s) noexcept {
  return s == NmState::kRepeatMessage || s == NmState::kNormalOperation;
}
} // namespace

Result<std::unique_ptr<NetworkHandle>> NetworkHandle::Create(std::string name, NmConfig cfg,
                                                              std::shared_ptr<NmClock> clock) {
  using R = Result<std::unique_ptr<NetworkHandle>>;
  if (!clock || cfg.repeatMessageTime.count() <= 0 || cfg.nmTimeout.count() <= 0 ||
      cfg.waitBusSleepTime.count() <= 0 || cfg.messageCycleTime.count() <= 0 ||
      cfg.pnResetTime.count() <= 0) {
    return R::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  }
  return R::FromValue(
      std::unique_ptr<NetworkHandle>(new NetworkHandle(std::move(name), cfg, std::move(clock))));
}

NetworkHandle::NetworkHandle(std::string name, NmConfig cfg, std::shared_ptr<NmClock> clock)
    : name_(std::move(name)), cfg_(cfg), clock_(std::move(clock)) {}

NmState NetworkHandle::GetState() const {
  std::lock_guard<std::mutex> l(mu_);
  return state_;
}

bool NetworkHandle::IsRequested() const {
  std::lock_guard<std::mutex> l(mu_);
  return requested_;
}

void NetworkHandle::SetStateChangeHandler(StateHandler h) {
  std::lock_guard<std::mutex> l(mu_);
  onState_ = std::move(h);
}

void NetworkHandle::SetTxHandler(TxHandler h) {
  std::lock_guard<std::mutex> l(mu_);
  onTx_ = std::move(h);
}

void NetworkHandle::Enter(NmState s, Millis t, Pending &p) {
  if (s == state_) return;
  p.changes.emplace_back(state_, s);
  state_ = s;
  switch (s) {
  case NmState::kRepeatMessage:
    repeatDeadline_ = t + cfg_.repeatMessageTime;
    nmTimeoutDeadline_ = t + cfg_.nmTimeout;
    txDeadline_ = t; // send immediately
    break;
  case NmState::kNormalOperation:
    nmTimeoutDeadline_ = t + cfg_.nmTimeout;
    break;
  case NmState::kReadySleep: nmTimeoutDeadline_ = t + cfg_.nmTimeout; break;
  case NmState::kPrepareBusSleep: sleepDeadline_ = t + cfg_.waitBusSleepTime; break;
  case NmState::kBusSleep: break;
  }
}

// Executes at most one timer-driven transition; returns true if one happened.
bool NetworkHandle::Step(Millis now, Pending &p) {
  switch (state_) {
  case NmState::kRepeatMessage:
    if (now >= repeatDeadline_) {
      Enter(requested_ ? NmState::kNormalOperation : NmState::kReadySleep, repeatDeadline_, p);
      return true;
    }
    return false;
  case NmState::kReadySleep:
    if (now >= nmTimeoutDeadline_) {
      Enter(NmState::kPrepareBusSleep, nmTimeoutDeadline_, p);
      return true;
    }
    return false;
  case NmState::kPrepareBusSleep:
    if (now >= sleepDeadline_) {
      Enter(NmState::kBusSleep, sleepDeadline_, p);
      return true;
    }
    return false;
  case NmState::kNormalOperation:
  case NmState::kBusSleep: return false;
  }
  return false;
}

NmPdu NetworkHandle::MakePdu() const {
  NmPdu pdu;
  pdu.sourceNodeId = cfg_.nodeId;
  pdu.repeatMessageRequest = state_ == NmState::kRepeatMessage;
  pdu.partialNetworkInfo = cfg_.pnFilterMask != 0;
  pdu.pnRequest = pdu.partialNetworkInfo ? (localPn_ & cfg_.pnFilterMask) : 0U;
  return pdu;
}

void NetworkHandle::Pump(Millis now, Pending &p) {
  while (Step(now, p)) {
  }
  for (std::uint8_t i = 0; i < kMaxPartialNetworks; ++i) {
    std::uint32_t const bit = 1U << i;
    if ((remotePn_ & bit) != 0 && now >= remotePnExpiry_[i]) remotePn_ &= ~bit;
  }
  if (Transmits(state_) && now >= txDeadline_) {
    p.tx.push_back(MakePdu());
    txDeadline_ = now + cfg_.messageCycleTime;
  }
}

void NetworkHandle::Dispatch(Pending &p) {
  StateHandler sh;
  TxHandler th;
  {
    std::lock_guard<std::mutex> l(mu_);
    sh = onState_;
    th = onTx_;
  }
  if (sh) for (auto const &c : p.changes) sh(c.first, c.second);
  if (th) for (auto const &m : p.tx) th(m);
}

Result<void> NetworkHandle::Request() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p); // bring timers up to date before applying the request
    requested_ = true;
    switch (state_) {
    case NmState::kBusSleep:
    case NmState::kPrepareBusSleep: Enter(NmState::kRepeatMessage, now, p); break;
    case NmState::kReadySleep:
      Enter(NmState::kNormalOperation, now, p);
      txDeadline_ = now;
      break;
    case NmState::kRepeatMessage:
    case NmState::kNormalOperation: break;
    }
    Pump(now, p);
  }
  Dispatch(p);
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::Release() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p);
    if (!requested_) return Result<void>::FromError(MakeErrorCode(NmErrc::kNotRequested));
    requested_ = false;
    if (state_ == NmState::kNormalOperation) Enter(NmState::kReadySleep, now, p);
    Pump(now, p);
  }
  Dispatch(p);
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::RequestPartialNetwork(std::uint8_t pnId) {
  if (pnId >= kMaxPartialNetworks) return Result<void>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  std::lock_guard<std::mutex> l(mu_);
  localPn_ |= 1U << pnId;
  return Result<void>::FromValue();
}

Result<void> NetworkHandle::ReleasePartialNetwork(std::uint8_t pnId) {
  if (pnId >= kMaxPartialNetworks) return Result<void>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  std::lock_guard<std::mutex> l(mu_);
  localPn_ &= ~(1U << pnId);
  return Result<void>::FromValue();
}

Result<bool> NetworkHandle::IsPartialNetworkRequested(std::uint8_t pnId) {
  if (pnId >= kMaxPartialNetworks) return Result<bool>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  Pending p;
  bool r = false;
  {
    std::lock_guard<std::mutex> l(mu_);
    Pump(clock_->Now(), p);
    r = ((localPn_ | remotePn_) & (1U << pnId)) != 0;
  }
  Dispatch(p);
  return Result<bool>::FromValue(r);
}

void NetworkHandle::OnPduReceived(NmPdu const &pdu) {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p);
    nmTimeoutDeadline_ = now + cfg_.nmTimeout;
    if (pdu.partialNetworkInfo) {
      std::uint32_t const bits = pdu.pnRequest & cfg_.pnFilterMask;
      for (std::uint8_t i = 0; i < kMaxPartialNetworks; ++i) {
        if ((bits & (1U << i)) != 0) {
          remotePn_ |= 1U << i;
          remotePnExpiry_[i] = now + cfg_.pnResetTime;
        }
      }
    }
    switch (state_) {
    case NmState::kBusSleep:
    case NmState::kPrepareBusSleep: Enter(NmState::kRepeatMessage, now, p); break;
    case NmState::kReadySleep:
    case NmState::kNormalOperation:
      if (pdu.repeatMessageRequest) Enter(NmState::kRepeatMessage, now, p);
      break;
    case NmState::kRepeatMessage: break;
    }
    Pump(now, p);
  }
  Dispatch(p);
}

Result<void> NetworkHandle::OnBytesReceived(std::uint8_t const *bytes, std::size_t size) {
  auto pdu = NmPdu::Decode(bytes, size);
  if (!pdu.HasValue()) return Result<void>::FromError(pdu.Error());
  OnPduReceived(pdu.Value());
  return Result<void>::FromValue();
}

void NetworkHandle::Process() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Pump(clock_->Now(), p);
  }
  Dispatch(p);
}

} // namespace ara::nm
