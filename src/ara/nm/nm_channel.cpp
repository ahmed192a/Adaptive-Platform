#include "ara/nm/nm_channel.h"

#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

using ara::core::Result;

namespace {
bool Transmits(NmState s) noexcept {
  return s == NmState::kRepeatMessage || s == NmState::kNormalOperation;
}
} // namespace

Result<std::unique_ptr<NmChannel>> NmChannel::Create(std::string name, NmConfig cfg,
                                                              std::shared_ptr<NmClock> clock) {
  using R = Result<std::unique_ptr<NmChannel>>;
  if (!clock || cfg.repeatMessageTime.count() <= 0 || cfg.nmTimeout.count() <= 0 ||
      cfg.waitBusSleepTime.count() <= 0 || cfg.messageCycleTime.count() <= 0 ||
      cfg.pnResetTime.count() <= 0) {
    return R::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  }
  // SWS_ANM_00056: the PN reset time must be longer than the NM message cycle time.
  if (cfg.pnFilterMask != 0 && cfg.pnResetTime <= cfg.messageCycleTime) {
    return R::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  }
  if (cfg.immediateNmTransmissions != 0 && cfg.immediateNmCycleTime.count() <= 0) {
    return R::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  }
  return R::FromValue(
      std::unique_ptr<NmChannel>(new NmChannel(std::move(name), cfg, std::move(clock))));
}

NmChannel::NmChannel(std::string name, NmConfig cfg, std::shared_ptr<NmClock> clock)
    : name_(std::move(name)), cfg_(cfg), clock_(std::move(clock)) {}

NmState NmChannel::GetState() const {
  std::lock_guard<std::mutex> l(mu_);
  return state_;
}

bool NmChannel::IsRequested() const {
  std::lock_guard<std::mutex> l(mu_);
  return requested_;
}

void NmChannel::SetStateChangeHandler(StateHandler h) {
  std::lock_guard<std::mutex> l(mu_);
  onState_ = std::move(h);
}

void NmChannel::SetTxHandler(TxHandler h) {
  std::lock_guard<std::mutex> l(mu_);
  onTx_ = std::move(h);
}

void NmChannel::Enter(NmState s, Millis t, Pending &p) {
  if (s == state_) return;
  p.changes.emplace_back(state_, s);
  state_ = s;
  switch (s) {
  case NmState::kRepeatMessage:
    repeatDeadline_ = t + cfg_.repeatMessageTime;
    nmTimeoutDeadline_ = t + cfg_.nmTimeout;
    txDeadline_ = t; // send immediately
    immediateRemaining_ = cfg_.immediateNmTransmissions;
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
bool NmChannel::Step(Millis now, Pending &p) {
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

NmPdu NmChannel::MakePdu() const {
  NmPdu pdu;
  pdu.sourceNodeId = cfg_.nodeId;
  pdu.repeatMessageRequest = false; // SWS_ANM_00037: transmitted Repeat Message Request bit is always 0
  pdu.partialNetworkInfo = cfg_.pnFilterMask != 0;
  pdu.pnRequest = pdu.partialNetworkInfo ? (localPn_ & cfg_.pnFilterMask) : 0U;
  return pdu;
}

void NmChannel::Pump(Millis now, Pending &p) {
  while (Step(now, p)) {
  }
  for (std::uint8_t i = 0; i < kMaxPartialNetworks; ++i) {
    std::uint32_t const bit = 1U << i;
    if ((remotePn_ & bit) != 0 && now >= remotePnExpiry_[i]) remotePn_ &= ~bit;
  }
  if (Transmits(state_) && now >= txDeadline_) {
    p.tx.push_back(MakePdu());
    nmTimeoutDeadline_ = now + cfg_.nmTimeout; // SWS_ANM_00007..09: restarted on NM Tx as well as Rx
    if (immediateRemaining_ > 0) {
      --immediateRemaining_;
      txDeadline_ = now + cfg_.immediateNmCycleTime;
    } else {
      txDeadline_ = now + cfg_.messageCycleTime;
    }
  }
}

void NmChannel::Dispatch(Pending &p) {
  StateHandler sh;
  TxHandler th;
  std::vector<std::pair<std::size_t, StateHandler>> so;
  std::vector<std::pair<std::size_t, RequestedHandler>> ro;
  {
    std::lock_guard<std::mutex> l(mu_);
    sh = onState_;
    th = onTx_;
    so = stateObservers_;
    ro = requestedObservers_;
  }
  for (auto const &c : p.changes) {
    if (sh) sh(c.first, c.second);
    for (auto const &o : so) o.second(c.first, c.second);
  }
  for (bool r : p.requestedChanges) {
    for (auto const &o : ro) o.second(r);
  }
  if (th) for (auto const &m : p.tx) th(m);
}

Result<void> NmChannel::Request() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p); // bring timers up to date before applying the request
    if (!requested_) p.requestedChanges.push_back(true);
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

Result<void> NmChannel::Release() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p);
    if (!requested_) return Result<void>::FromError(MakeErrorCode(NmErrc::kNotRequested));
    requested_ = false;
    p.requestedChanges.push_back(false);
    if (state_ == NmState::kNormalOperation) Enter(NmState::kReadySleep, now, p);
    Pump(now, p);
  }
  Dispatch(p);
  return Result<void>::FromValue();
}

Result<void> NmChannel::RequestPartialNetwork(std::uint8_t pnId) {
  if (pnId >= kMaxPartialNetworks) return Result<void>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  std::lock_guard<std::mutex> l(mu_);
  localPn_ |= 1U << pnId;
  return Result<void>::FromValue();
}

Result<void> NmChannel::ReleasePartialNetwork(std::uint8_t pnId) {
  if (pnId >= kMaxPartialNetworks) return Result<void>::FromError(MakeErrorCode(NmErrc::kUnknownNetwork));
  std::lock_guard<std::mutex> l(mu_);
  localPn_ &= ~(1U << pnId);
  return Result<void>::FromValue();
}

Result<bool> NmChannel::IsPartialNetworkRequested(std::uint8_t pnId) {
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

void NmChannel::OnPduReceived(NmPdu const &pdu) {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p);
    // SWS_ANM_00081/00089: with partial networking active, drop PNI=1 messages that carry no
    // bit of a partial network this node serves (unless allNmMessagesKeepAwake).
    bool const dropped = cfg_.pnFilterMask != 0 && pdu.partialNetworkInfo &&
                         !cfg_.allNmMessagesKeepAwake && (pdu.pnRequest & cfg_.pnFilterMask) == 0;
    if (!dropped) {
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
    }
    Pump(now, p);
  }
  Dispatch(p);
}

Result<void> NmChannel::OnBytesReceived(std::uint8_t const *bytes, std::size_t size) {
  auto pdu = NmPdu::Decode(bytes, size);
  if (!pdu.HasValue()) return Result<void>::FromError(pdu.Error());
  OnPduReceived(pdu.Value());
  return Result<void>::FromValue();
}

void NmChannel::Process() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Pump(clock_->Now(), p);
  }
  Dispatch(p);
}

} // namespace ara::nm

namespace ara::nm {

std::size_t NmChannel::AddStateObserver(StateHandler h) {
  std::lock_guard<std::mutex> l(mu_);
  std::size_t const id = nextObserverId_++;
  stateObservers_.emplace_back(id, std::move(h));
  return id;
}

void NmChannel::RemoveStateObserver(std::size_t id) {
  std::lock_guard<std::mutex> l(mu_);
  for (auto it = stateObservers_.begin(); it != stateObservers_.end(); ++it) {
    if (it->first == id) { stateObservers_.erase(it); return; }
  }
}

std::size_t NmChannel::AddRequestedObserver(RequestedHandler h) {
  std::lock_guard<std::mutex> l(mu_);
  std::size_t const id = nextObserverId_++;
  requestedObservers_.emplace_back(id, std::move(h));
  return id;
}

void NmChannel::RemoveRequestedObserver(std::size_t id) {
  std::lock_guard<std::mutex> l(mu_);
  for (auto it = requestedObservers_.begin(); it != requestedObservers_.end(); ++it) {
    if (it->first == id) { requestedObservers_.erase(it); return; }
  }
}

void NmChannel::Withdraw() {
  Pending p;
  {
    std::lock_guard<std::mutex> l(mu_);
    Millis const now = clock_->Now();
    Pump(now, p);
    localPn_ = 0;
    if (requested_) {
      requested_ = false;
      p.requestedChanges.push_back(false);
      if (state_ == NmState::kNormalOperation) Enter(NmState::kReadySleep, now, p);
    }
    Pump(now, p);
  }
  Dispatch(p);
}

} // namespace ara::nm
