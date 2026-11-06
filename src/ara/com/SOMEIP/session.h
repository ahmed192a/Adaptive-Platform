/**
 * @file session.h
 * @brief SOME/IP session handling: Session ID counter, request/response matching and the
 *        receiver-side reboot detection of SOME/IP-SD.
 *
 * Ids in the comments are PRS_SOMEIP_xxxxx / PRS_SOMEIPSD_xxxxx as listed in
 * doc/spec-conformance/com.md. The code is transport independent.
 */
#ifndef ARA_COM_SOMEIP_SESSION_H_
#define ARA_COM_SOMEIP_SESSION_H_

#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace ara::com::someip {

/// Session ID generator. Active: 1, 2, ..., 0xFFFF, 1, ... (never 0) [PRS_SOMEIP_00933, 00521].
/// Inactive: always 0 [PRS_SOMEIP_00932]. For SD the Reboot flag is set until the counter wrapped
/// for the first time [PRS_SOMEIPSD_00255].
class SessionIdCounter {
public:
  explicit SessionIdCounter(bool active = true) : active_(active) {}

  std::uint16_t Next() {
    std::lock_guard<std::mutex> g(m_);
    if (!active_) return 0;
    std::uint16_t const id = next_;
    if (next_ == 0xFFFF) {
      next_ = 1;
      wrapped_ = true;
    } else {
      ++next_;
    }
    return id;
  }

  bool RebootFlag() const {
    std::lock_guard<std::mutex> g(m_);
    return !wrapped_;
  }

  /// Start over (e.g. after the SD instance restarted): next id is 1 and the Reboot flag is set again.
  void Reset() {
    std::lock_guard<std::mutex> g(m_);
    next_ = 1;
    wrapped_ = false;
  }

private:
  mutable std::mutex m_;
  bool active_;
  std::uint16_t next_{1};
  bool wrapped_{false};
};

/// Matches responses to outstanding requests by (Client ID, Session ID) [PRS_SOMEIP_00533, 00739].
class RequestTracker {
public:
  struct Response {
    std::uint8_t returnCode{0};
    std::vector<std::uint8_t> payload;
    bool cancelled{false}; ///< the tracker was shut down before a response arrived
  };

  /// Register an outgoing request; the future becomes ready when Complete() sees the response.
  /// Returns an invalid future if the key is already pending.
  std::future<Response> Register(std::uint16_t clientId, std::uint16_t sessionId) {
    std::lock_guard<std::mutex> g(m_);
    auto key = std::make_pair(clientId, sessionId);
    if (pending_.count(key)) return {};
    auto &p = pending_[key];
    return p.get_future();
  }

  /// A response arrived. False when no such request is pending (late, duplicate or unsolicited).
  bool Complete(std::uint16_t clientId, std::uint16_t sessionId, std::uint8_t returnCode,
                std::vector<std::uint8_t> payload) {
    std::promise<Response> p;
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = pending_.find({clientId, sessionId});
      if (it == pending_.end()) return false;
      p = std::move(it->second);
      pending_.erase(it);
    }
    Response r;
    r.returnCode = returnCode;
    r.payload = std::move(payload);
    p.set_value(std::move(r));
    return true;
  }

  /// Drop one request (e.g. on timeout). Its future yields a cancelled response.
  bool Cancel(std::uint16_t clientId, std::uint16_t sessionId) {
    std::promise<Response> p;
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = pending_.find({clientId, sessionId});
      if (it == pending_.end()) return false;
      p = std::move(it->second);
      pending_.erase(it);
    }
    Response r;
    r.cancelled = true;
    p.set_value(std::move(r));
    return true;
  }

  /// Cancel everything (shutdown / lost connection).
  void CancelAll() {
    std::map<std::pair<std::uint16_t, std::uint16_t>, std::promise<Response>> all;
    {
      std::lock_guard<std::mutex> g(m_);
      all.swap(pending_);
    }
    for (auto &kv : all) {
      Response r;
      r.cancelled = true;
      kv.second.set_value(std::move(r));
    }
  }

  std::size_t Pending() const {
    std::lock_guard<std::mutex> g(m_);
    return pending_.size();
  }

private:
  mutable std::mutex m_;
  std::map<std::pair<std::uint16_t, std::uint16_t>, std::promise<Response>> pending_;
};

/// Receiver-side reboot detection of a remote SD peer. Unicast and multicast SD messages keep
/// separate session counters [PRS_SOMEIPSD_00160, 00256]. A reboot is detected when the Reboot flag
/// goes 0 -> 1, or stays 1 while the Session ID does not increase [PRS_SOMEIPSD_00258].
class SdRebootDetector {
public:
  /// Feed one received SD message of `peer`. Returns true when the peer rebooted, in which case the
  /// caller should drop everything it knows about the peer (offers, subscriptions).
  bool Observe(std::uint32_t peer, bool multicast, bool rebootFlag, std::uint16_t sessionId) {
    std::lock_guard<std::mutex> g(m_);
    auto &st = peers_[{peer, multicast}];
    bool rebooted = false;
    if (st.seen) {
      if (!st.rebootFlag && rebootFlag) rebooted = true;                       // flag cleared -> set again
      else if (st.rebootFlag && rebootFlag && sessionId <= st.sessionId) rebooted = true;
    }
    st.seen = true;
    st.rebootFlag = rebootFlag;
    st.sessionId = sessionId;
    return rebooted;
  }

  void Forget(std::uint32_t peer) {
    std::lock_guard<std::mutex> g(m_);
    peers_.erase({peer, false});
    peers_.erase({peer, true});
  }

private:
  struct State {
    bool seen{false};
    bool rebootFlag{false};
    std::uint16_t sessionId{0};
  };
  std::mutex m_;
  std::map<std::pair<std::uint32_t, bool>, State> peers_;
};

} // namespace ara::com::someip

#endif // ARA_COM_SOMEIP_SESSION_H_
