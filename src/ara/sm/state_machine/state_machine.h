/**
 * @file state_machine.h
 * @brief Configurable State Management model: StateMachine with RequestTransition, the
 *        StateManager that owns the machine-level state machines and the initial transition,
 *        the function-group RecoveryHandler registry and the NetworkHandle bridge.
 *
 * Header-only and transport independent (no sockets, no Execution Management): the SM application
 * plugs its own actions in (for example "ask Execution Management to set function group X to state Y").
 * Requirement ids are SWS_SM_xxxxx as named in doc/spec-conformance/sm.md; the numeric values of
 * the error enumerators are *Unverified* (chapter 8 of the SWS could not be read).
 */
#ifndef ARA_SM_STATE_MACHINE_STATE_MACHINE_H_
#define ARA_SM_STATE_MACHINE_STATE_MACHINE_H_

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ara::sm::state_machine {

/// Errors of StateMachineService::RequestTransition (SWS_SM_00603 .. 00606, 00650).
enum class TransitionErrc : std::uint8_t {
  kTransitionNotAllowed = 1,        ///< [SWS_SM_00603] no transition from the current state to the requested one
  kInvalidValue = 2,                ///< [SWS_SM_00604] the requested state does not exist
  kRecoveryTransitionOngoing = 3,   ///< [SWS_SM_00605] a recovery transition is running
  kOperationCanceled = 4,           ///< [SWS_SM_00606] a newer valid request aborted this one ([SWS_SM_00607])
  kTransitionFailed = 5             ///< [SWS_SM_00650] an action of the ActionList failed
};

/// nullopt == success.
using TransitionResult = std::optional<TransitionErrc>;

/// One step of a transition. Returns true on success; false makes the transition fail.
struct Action {
  std::string name;
  std::function<bool()> run;
};
using ActionList = std::vector<Action>;

/// The states every machine-level "Update" state machine must provide (section 7.7.1).
inline std::vector<std::string> const &MinimumStates() {
  static const std::vector<std::string> kStates{"Initial", "Off", "PrepareUpdate", "VerifyUpdate",
                                                "PrepareRollback"};
  return kStates;
}

class StateMachine {
 public:
  using Notifier = std::function<void(std::string const &from, std::string const &to)>;

  StateMachine(std::string name, std::vector<std::string> states, std::string initial)
      : name_(std::move(name)), states_(states.begin(), states.end()), current_(std::move(initial)) {
    if (!states_.count(current_)) states_.insert(current_);
  }

  std::string const &Name() const { return name_; }

  std::string CurrentState() const {
    std::lock_guard<std::mutex> g(m_);
    return current_;
  }

  /// True when the machine has every state of MinimumStates().
  bool HasMinimumStates() const {
    std::lock_guard<std::mutex> g(m_);
    return std::all_of(MinimumStates().begin(), MinimumStates().end(),
                       [&](std::string const &s) { return states_.count(s) != 0; });
  }

  /// Allow `from` -> `to` (`from` == "*" allows it from any state) and run `actions` for it.
  /// Both states are added to the machine if they are new.
  void AddTransition(std::string from, std::string to, ActionList actions = {}) {
    std::lock_guard<std::mutex> g(m_);
    if (from != "*") states_.insert(from);
    states_.insert(to);
    transitions_[{std::move(from), std::move(to)}] = std::move(actions);
  }

  /// [SWS_SM_00665] StateMachineNotification: called after every committed state change.
  void SetNotifier(Notifier n) {
    std::lock_guard<std::mutex> g(m_);
    notifier_ = std::move(n);
  }

  /// [SWS_SM_00618] / [00619] the service is only offered once the machine is initialised and is
  /// stopped before shutdown; while it is not offered every request is refused.
  void SetOffered(bool offered) {
    std::lock_guard<std::mutex> g(m_);
    offered_ = offered;
  }
  bool Offered() const {
    std::lock_guard<std::mutex> g(m_);
    return offered_;
  }

  /// While a recovery transition runs, normal requests are refused ([SWS_SM_00605]).
  void BeginRecovery() { recovery_ = true; }
  void EndRecovery() { recovery_ = false; }

  /// [SWS_SM_00600] RequestTransition. Blocks until the ActionList finished. A newer valid request
  /// aborts a running one ([SWS_SM_00607]): the older caller then gets kOperationCanceled and the
  /// state stays unchanged until the newer request completes.
  TransitionResult RequestTransition(std::string const &to) {
    std::vector<Action> actions;
    std::string from;
    std::uint64_t myGen;
    {
      std::lock_guard<std::mutex> g(m_);
      if (!offered_) return TransitionErrc::kTransitionNotAllowed;
      if (recovery_) return TransitionErrc::kRecoveryTransitionOngoing;
      if (!states_.count(to)) return TransitionErrc::kInvalidValue;
      auto it = transitions_.find({current_, to});
      if (it == transitions_.end()) it = transitions_.find({"*", to});
      if (it == transitions_.end()) return TransitionErrc::kTransitionNotAllowed;
      actions = it->second;
      from = current_;
      myGen = ++generation_; // invalidates every older request still running
    }
    for (auto const &a : actions) {
      if (generation_.load() != myGen) return TransitionErrc::kOperationCanceled;
      if (a.run && !a.run()) {
        std::lock_guard<std::mutex> g(m_);
        return generation_ == myGen ? TransitionResult(TransitionErrc::kTransitionFailed)
                                    : TransitionResult(TransitionErrc::kOperationCanceled);
      }
    }
    Notifier notify;
    {
      std::lock_guard<std::mutex> g(m_);
      if (generation_ != myGen) return TransitionErrc::kOperationCanceled;
      current_ = to;
      notify = notifier_;
    }
    if (notify) notify(from, to);
    return std::nullopt;
  }

 private:
  std::string name_;
  mutable std::mutex m_;
  std::set<std::string> states_;
  std::string current_;
  std::map<std::pair<std::string, std::string>, ActionList> transitions_;
  Notifier notifier_;
  bool offered_{true};
  std::atomic<bool> recovery_{false};
  std::atomic<std::uint64_t> generation_{0};
};

/// Owns the machine-level state machines and performs the start-up transition whose result the
/// application can query afterwards (section 5.2, GetInitialMachineStateTransitionResult).
class StateManager {
 public:
  StateMachine &Add(std::string name, std::vector<std::string> states, std::string initial) {
    std::lock_guard<std::mutex> g(m_);
    auto sm = std::make_unique<StateMachine>(name, std::move(states), std::move(initial));
    StateMachine &ref = *sm;
    machines_[name] = std::move(sm);
    return ref;
  }

  StateMachine *Find(std::string const &name) {
    std::lock_guard<std::mutex> g(m_);
    auto it = machines_.find(name);
    return it == machines_.end() ? nullptr : it->second.get();
  }

  /// Request the start-up state of `machine`, remember the outcome and start offering the service.
  TransitionResult Start(std::string const &machine, std::string const &initialState) {
    StateMachine *sm = Find(machine);
    TransitionResult r = sm ? sm->RequestTransition(initialState) : TransitionResult(TransitionErrc::kInvalidValue);
    {
      std::lock_guard<std::mutex> g(m_);
      initialResult_ = r;
      initialDone_ = true;
    }
    return r;
  }

  /// nullopt (as outer value) until Start() ran; then the transition outcome.
  std::optional<TransitionResult> GetInitialMachineStateTransitionResult() const {
    std::lock_guard<std::mutex> g(m_);
    if (!initialDone_) return std::nullopt;
    return initialResult_;
  }

 private:
  mutable std::mutex m_;
  std::map<std::string, std::unique_ptr<StateMachine>> machines_;
  TransitionResult initialResult_;
  bool initialDone_{false};
};

// --------------------------------------------------------------------------- recovery
/// Return value of the recovery handler for a function group it does not know ([SWS_SM_00030/31]).
enum class RecoveryErrc : std::uint8_t { kSMCanNotHandleRecovery = 1 };
using RecoveryResult = std::optional<RecoveryErrc>;

/// Function-group recovery: PHM / Execution Management report a failed function group to State
/// Management; the SM runs the registered handler or answers kSMCanNotHandleRecovery.
class RecoveryHandler {
 public:
  using Handler = std::function<bool(std::string const &functionGroup)>;

  void Register(std::string functionGroup, Handler h) {
    std::lock_guard<std::mutex> g(m_);
    handlers_[std::move(functionGroup)] = std::move(h);
  }

  RecoveryResult Handle(std::string const &functionGroup) {
    Handler h;
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = handlers_.find(functionGroup);
      if (it == handlers_.end()) return RecoveryErrc::kSMCanNotHandleRecovery;
      h = it->second;
    }
    return (h && h(functionGroup)) ? RecoveryResult() : RecoveryResult(RecoveryErrc::kSMCanNotHandleRecovery);
  }

 private:
  std::mutex m_;
  std::map<std::string, Handler> handlers_;
};

// ------------------------------------------------------------------- network handles
enum class NetworkHandleState : std::uint8_t { kNoCom = 0, kFullCom = 1 };

/// [SWS_SM_00620] / [00621]: a network handle changing between NoCom and FullCom triggers the
/// state-machine request configured for that change. `SetNetworkHandle` records what the
/// application wants and returns it to the (Network Management) caller via `OnRequest`.
class NetworkHandleBridge {
 public:
  explicit NetworkHandleBridge(StateMachine &sm) : sm_(sm) {}

  /// When `handle` reaches `state`, request `targetState` on the state machine.
  void Map(std::string handle, NetworkHandleState state, std::string targetState) {
    std::lock_guard<std::mutex> g(m_);
    map_[{std::move(handle), state}] = std::move(targetState);
  }

  /// Called by Network Management when the handle changed state.
  TransitionResult OnNetworkState(std::string const &handle, NetworkHandleState state) {
    std::string target;
    {
      std::lock_guard<std::mutex> g(m_);
      current_[handle] = state;
      auto it = map_.find({handle, state});
      if (it == map_.end()) return std::nullopt; // nothing configured for this change
      target = it->second;
    }
    return sm_.RequestTransition(target);
  }

  /// SetNetworkHandle: remember the state the SM wants (and tell the requester).
  void SetNetworkHandle(std::string const &handle, NetworkHandleState wanted) {
    std::function<void(std::string const &, NetworkHandleState)> cb;
    {
      std::lock_guard<std::mutex> g(m_);
      wanted_[handle] = wanted;
      cb = onRequest_;
    }
    if (cb) cb(handle, wanted);
  }

  void OnRequest(std::function<void(std::string const &, NetworkHandleState)> cb) {
    std::lock_guard<std::mutex> g(m_);
    onRequest_ = std::move(cb);
  }

  std::optional<NetworkHandleState> Current(std::string const &handle) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = current_.find(handle);
    return it == current_.end() ? std::nullopt : std::optional<NetworkHandleState>(it->second);
  }
  std::optional<NetworkHandleState> Wanted(std::string const &handle) const {
    std::lock_guard<std::mutex> g(m_);
    auto it = wanted_.find(handle);
    return it == wanted_.end() ? std::nullopt : std::optional<NetworkHandleState>(it->second);
  }

 private:
  StateMachine &sm_;
  mutable std::mutex m_;
  std::map<std::pair<std::string, NetworkHandleState>, std::string> map_;
  std::map<std::string, NetworkHandleState> current_, wanted_;
  std::function<void(std::string const &, NetworkHandleState)> onRequest_;
};

} // namespace ara::sm::state_machine

#endif // ARA_SM_STATE_MACHINE_STATE_MACHINE_H_
