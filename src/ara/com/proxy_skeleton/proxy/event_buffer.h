/**
 * @file event_buffer.h
 * @brief Spec-shaped proxy-side event sample queue (SWS_CommunicationManagement ProxyEvent API):
 *        Subscribe(maxSampleCount), Unsubscribe, GetSubscriptionState, SetSubscriptionStateChangeHandler,
 *        GetNewSamples, GetFreeSampleCount, SetReceiveHandler / UnsetReceiveHandler.
 *
 * Transport independent: the binding calls Push() for each received event and OnSubscriptionAck()
 * when the service acknowledged (or refused) the subscription. API names are those of the SWS index
 * (R24-11); the exact signatures and the overflow policy were not readable and are *Unverified*.
 * Decisions: a full queue drops its OLDEST sample; GetNewSamples on a non-subscribed buffer yields 0 samples.
 */
#ifndef ARA_COM_PROXY_SKELETON_PROXY_EVENT_BUFFER_H_
#define ARA_COM_PROXY_SKELETON_PROXY_EVENT_BUFFER_H_

#include "ara/com/com_error_domain.h"
#include "ara/com/proxy_skeleton/data_type.h"
#include "ara/core/result.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace ara::com {

/// Read-only pointer to a received sample. The sample counts against the subscription's
/// maxSampleCount until the last copy of the pointer is destroyed.
template <typename T> using SamplePtr = std::shared_ptr<const T>;

template <typename T> class ProxyEventBuffer {
public:
  /// `maxRealizable`: the largest maxSampleCount the binding can honour.
  explicit ProxyEventBuffer(std::size_t maxRealizable = 1024)
      : maxRealizable_(maxRealizable), held_(std::make_shared<std::atomic<std::size_t>>(0)) {}

  /// Subscribe with room for `maxSampleCount` samples (queued + held by the application).
  /// kMaxSampleCountNotRealizable for 0 or more than the binding can hold. Calling it again while
  /// subscribed is a no-op. The state stays kSubscriptionPending until OnSubscriptionAck(true).
  ara::core::Result<void> Subscribe(std::size_t maxSampleCount) {
    SubscriptionStateChangeHandler h;
    {
      std::lock_guard<std::mutex> g(m_);
      if (maxSampleCount == 0 || maxSampleCount > maxRealizable_)
        return ara::core::Result<void>::FromError(MakeErrorCode(ComErrc::kMaxSampleCountNotRealizable));
      if (state_ != SubscriptionState::kNotSubscribed) return ara::core::Result<void>::FromValue();
      max_ = maxSampleCount;
      state_ = SubscriptionState::kSubscriptionPending;
      h = stateHandler_;
    }
    if (h) h(SubscriptionState::kSubscriptionPending);
    return ara::core::Result<void>::FromValue();
  }

  void Unsubscribe() {
    SubscriptionStateChangeHandler h;
    {
      std::lock_guard<std::mutex> g(m_);
      if (state_ == SubscriptionState::kNotSubscribed) return;
      state_ = SubscriptionState::kNotSubscribed;
      queue_.clear();
      h = stateHandler_;
    }
    if (h) h(SubscriptionState::kNotSubscribed);
  }

  SubscriptionState GetSubscriptionState() const {
    std::lock_guard<std::mutex> g(m_);
    return state_;
  }

  void SetSubscriptionStateChangeHandler(SubscriptionStateChangeHandler h) {
    std::lock_guard<std::mutex> g(m_);
    stateHandler_ = std::move(h);
  }
  void UnsetSubscriptionStateChangeHandler() {
    std::lock_guard<std::mutex> g(m_);
    stateHandler_ = nullptr;
  }

  /// Binding: the service acknowledged (true) or refused (false) the subscription.
  void OnSubscriptionAck(bool accepted) {
    SubscriptionStateChangeHandler h;
    SubscriptionState s;
    {
      std::lock_guard<std::mutex> g(m_);
      if (state_ != SubscriptionState::kSubscriptionPending && state_ != SubscriptionState::kSubscribed) return;
      s = accepted ? SubscriptionState::kSubscribed : SubscriptionState::kSubscriptionPending;
      if (s == state_) return;
      state_ = s;
      h = stateHandler_;
    }
    if (h) h(s);
  }

  void SetReceiveHandler(EventReceiveHandler h) {
    std::lock_guard<std::mutex> g(m_);
    receive_ = std::move(h);
  }
  void UnsetReceiveHandler() {
    std::lock_guard<std::mutex> g(m_);
    receive_ = nullptr;
  }

  /// Hand every new sample (oldest first), at most `maxNumberOfSamples`, to `f(SamplePtr<T>)`.
  /// Returns how many were delivered. kMaxSamplesReached when the application already holds
  /// maxSampleCount samples; delivery stops once that limit is reached.
  template <typename F>
  ara::core::Result<std::size_t> GetNewSamples(F &&f, std::size_t maxNumberOfSamples = std::numeric_limits<std::size_t>::max()) {
    std::size_t delivered = 0;
    while (delivered < maxNumberOfSamples) {
      SamplePtr<T> sp;
      {
        std::lock_guard<std::mutex> g(m_);
        if (state_ != SubscriptionState::kSubscribed) break;
        if (held_->load() >= max_)
          return delivered == 0 ? ara::core::Result<std::size_t>::FromError(MakeErrorCode(ComErrc::kMaxSamplesReached))
                                : ara::core::Result<std::size_t>::FromValue(delivered);
        if (queue_.empty()) break;
        auto counter = held_;
        counter->fetch_add(1);
        sp = SamplePtr<T>(new T(std::move(queue_.front())), [counter](T const *p) {
          delete p;
          counter->fetch_sub(1);
        });
        queue_.pop_front();
      }
      f(std::move(sp));
      ++delivered;
    }
    return ara::core::Result<std::size_t>::FromValue(delivered);
  }

  /// Samples that can still be received: maxSampleCount minus queued and held ones.
  std::size_t GetFreeSampleCount() const {
    std::lock_guard<std::mutex> g(m_);
    if (state_ == SubscriptionState::kNotSubscribed) return 0;
    std::size_t const used = queue_.size() + held_->load();
    return used >= max_ ? 0 : max_ - used;
  }

  /// Binding: a new event arrived. Dropped when not subscribed; when queued + held would exceed
  /// maxSampleCount the oldest queued sample is discarded.
  void Push(T value) {
    EventReceiveHandler h;
    {
      std::lock_guard<std::mutex> g(m_);
      if (state_ != SubscriptionState::kSubscribed) return;
      if (queue_.size() + held_->load() >= max_) {
        if (queue_.empty()) return; // everything is held by the application: the new sample is lost
        queue_.pop_front();
      }
      queue_.push_back(std::move(value));
      h = receive_;
    }
    if (h) h();
  }

private:
  std::size_t maxRealizable_;
  mutable std::mutex m_;
  SubscriptionState state_{SubscriptionState::kNotSubscribed};
  std::size_t max_{0};
  std::deque<T> queue_;
  std::shared_ptr<std::atomic<std::size_t>> held_;
  SubscriptionStateChangeHandler stateHandler_;
  EventReceiveHandler receive_;
};

} // namespace ara::com

#endif // ARA_COM_PROXY_SKELETON_PROXY_EVENT_BUFFER_H_
