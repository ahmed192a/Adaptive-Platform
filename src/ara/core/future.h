/**
 * @file future.h
 * @brief ara::core::Future<T,E> and ara::core::Promise<T,E> (SWS_CORE_00321 ff.)
 *
 * Exception-free futures built on ara::core::Result.  Differences to the
 * std:: types: the error channel is E (default ErrorCode) and a continuation
 * can be attached with then().
 *
 * Misuse is reported through FutureErrc (thrown as FutureException, or terminate
 * when ARA_NO_EXCEPTIONS is set): no_state for operations on an empty Future/Promise,
 * future_already_retrieved for a second get_future(), promise_already_satisfied for a
 * second set_value()/SetError(), and a Promise destroyed unfulfilled delivers
 * FutureErrc::broken_promise to its Future (when E is constructible from ErrorCode).
 */
#ifndef ARA_CORE_FUTURE_H_
#define ARA_CORE_FUTURE_H_

#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "ara/core/error_code.h"
#include "ara/core/future_error_domain.h"
#include "ara/core/result.h"

namespace ara::core {

enum class future_status : std::uint8_t { ready = 0, timeout = 1 };

template <typename T, typename E = ErrorCode> class Future;
template <typename T, typename E = ErrorCode> class Promise;

namespace detail {
[[noreturn]] inline void ThrowFuture(FutureErrc c) {
  ThrowOrTerminate<FutureException>(MakeErrorCode(c));
  std::terminate();
}

template <typename T, typename E> struct SharedState {
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<Result<T, E>> result;
  std::vector<std::function<void()>> continuations;
  bool futureRetrieved{false};

  /// Stores the result once; returns false if the state was already satisfied.
  bool Satisfy(Result<T, E> &&r) {
    std::vector<std::function<void()>> toRun;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (result.has_value()) {
        return false;
      }
      result.emplace(std::move(r));
      toRun.swap(continuations);
    }
    cv.notify_all();
    for (auto &c : toRun) {
      c();
    }
    return true;
  }
};

template <typename T> using NonVoid = std::conditional_t<std::is_void_v<T>, int, T>;

template <typename R> struct UnwrapResult {
  using value = R;
};
template <typename V, typename E> struct UnwrapResult<Result<V, E>> {
  using value = V;
};
} // namespace detail

template <typename T, typename E> class Future final {
  using State = detail::SharedState<T, E>;
  std::shared_ptr<State> mState;
  explicit Future(std::shared_ptr<State> s) noexcept : mState(std::move(s)) {}
  friend class Promise<T, E>;

public:
  using value_type = T;
  using error_type = E;

  Future() noexcept = default;
  Future(Future &&) noexcept = default;
  Future &operator=(Future &&) noexcept = default;
  Future(Future const &) = delete;
  Future &operator=(Future const &) = delete;
  ~Future() = default;

  bool valid() const noexcept { return mState != nullptr; }

  bool is_ready() const {
    if (!mState) {
      return false;
    }
    std::lock_guard<std::mutex> lock(mState->mutex);
    return mState->result.has_value();
  }

  void wait() const {
    RequireState();
    std::unique_lock<std::mutex> lock(mState->mutex);
    mState->cv.wait(lock, [&] { return mState->result.has_value(); });
  }

  template <typename Rep, typename Period>
  future_status wait_for(std::chrono::duration<Rep, Period> const &d) const {
    RequireState();
    std::unique_lock<std::mutex> lock(mState->mutex);
    return mState->cv.wait_for(lock, d, [&] { return mState->result.has_value(); })
               ? future_status::ready
               : future_status::timeout;
  }

  template <typename Clock, typename Duration>
  future_status wait_until(std::chrono::time_point<Clock, Duration> const &t) const {
    RequireState();
    std::unique_lock<std::mutex> lock(mState->mutex);
    return mState->cv.wait_until(lock, t, [&] { return mState->result.has_value(); })
               ? future_status::ready
               : future_status::timeout;
  }

  /// Blocks until ready and returns the Result; the Future becomes invalid.
  Result<T, E> GetResult() {
    wait();
    auto state = std::move(mState);
    mState.reset();
    std::lock_guard<std::mutex> lock(state->mutex);
    return std::move(*state->result);
  }

  /// Blocks; returns the value or throws (terminates without exceptions).
  T get() {
    if constexpr (std::is_void_v<T>) {
      GetResult().ValueOrThrow();
    } else {
      return std::move(GetResult()).ValueOrThrow();
    }
  }

  /// Attach a continuation.  `func` receives this Future (ready) and returns
  /// a plain value U or a Result<U,E>; then() returns a Future<U,E>.  The
  /// continuation runs on the thread that fulfils the Promise (or immediately
  /// on the calling thread if already ready).
  template <typename F> auto then(F &&func) {
    RequireState();
    using R = std::invoke_result_t<F, Future<T, E>>;
    using U = typename detail::UnwrapResult<R>::value;
    auto next = std::make_shared<detail::SharedState<U, E>>();
    auto state = std::move(mState);
    mState.reset();
    auto run = [state, next, fn = std::forward<F>(func)]() mutable {
      Future<T, E> ready(state);
      if constexpr (std::is_void_v<R>) {
        fn(std::move(ready));
        next->Satisfy(Result<void, E>());
      } else if constexpr (detail::IsResult<R>::value) {
        next->Satisfy(fn(std::move(ready)));
      } else {
        next->Satisfy(Result<U, E>(fn(std::move(ready))));
      }
    };
    {
      std::unique_lock<std::mutex> lock(state->mutex);
      if (!state->result.has_value()) {
        state->continuations.emplace_back(std::move(run));
        return Future<U, E>(next);
      }
    }
    run();
    return Future<U, E>(next);
  }

private:
  template <typename, typename> friend class Future;
  void RequireState() const {
    if (!mState) {
      detail::ThrowFuture(FutureErrc::no_state);
    }
  }
};

template <typename T, typename E> class Promise final {
  using State = detail::SharedState<T, E>;
  std::shared_ptr<State> mState{std::make_shared<State>()};

  /// Delivers broken_promise to a Future still waiting on an unfulfilled state.
  void Abandon() noexcept {
    if constexpr (std::is_constructible_v<E, ErrorCode>) {
      if (mState) {
        try {
          mState->Satisfy(Result<T, E>(E(MakeErrorCode(FutureErrc::broken_promise))));
        } catch (...) { // a continuation threw; nothing sensible left to do in a destructor
        }
      }
    }
    mState.reset();
  }
  State &Checked() const {
    if (!mState) {
      detail::ThrowFuture(FutureErrc::no_state);
    }
    return *mState;
  }
  void Set(Result<T, E> &&r) {
    if (!Checked().Satisfy(std::move(r))) {
      detail::ThrowFuture(FutureErrc::promise_already_satisfied);
    }
  }

public:
  Promise() = default;
  Promise(Promise &&other) noexcept : mState(std::move(other.mState)) { other.mState.reset(); }
  Promise &operator=(Promise &&other) noexcept {
    if (this != &other) {
      Abandon();
      mState = std::move(other.mState);
      other.mState.reset();
    }
    return *this;
  }
  Promise(Promise const &) = delete;
  Promise &operator=(Promise const &) = delete;
  ~Promise() { Abandon(); }

  /// May be called once; a second call fails with FutureErrc::future_already_retrieved.
  Future<T, E> get_future() {
    State &st = Checked();
    {
      std::lock_guard<std::mutex> lock(st.mutex);
      if (st.futureRetrieved) {
        detail::ThrowFuture(FutureErrc::future_already_retrieved);
      }
      st.futureRetrieved = true;
    }
    return Future<T, E>(mState);
  }

  template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
  void set_value(detail::NonVoid<T> const &value) {
    Set(Result<T, E>(value));
  }
  template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
  void set_value(detail::NonVoid<T> &&value) {
    Set(Result<T, E>(std::move(value)));
  }
  template <typename U = T, typename = std::enable_if_t<std::is_void_v<U>>> void set_value() {
    Set(Result<void, E>());
  }
  void SetError(E const &e) { Set(Result<T, E>(e)); }
  void SetError(E &&e) { Set(Result<T, E>(std::move(e))); }
  void SetResult(Result<T, E> &&r) { Set(std::move(r)); }
  /// Exchanges the shared state with another Promise (SWS_CORE_00340 family; no Abandon, nothing is lost).
  void Swap(Promise &other) noexcept { mState.swap(other.mState); }
};

template <typename T, typename E> void swap(Promise<T, E> &a, Promise<T, E> &b) noexcept { a.Swap(b); }

} // namespace ara::core

#endif // ARA_CORE_FUTURE_H_
