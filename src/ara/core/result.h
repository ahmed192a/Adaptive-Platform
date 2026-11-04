/**
 * @file result.h
 * @brief ara::core::Result<T, E> and Result<void, E>  (SWS_CORE_00701 ff.)
 *
 * A Result either contains a value of type T or an error of type E (default
 * ara::core::ErrorCode).  It is the exception-free error transport of all
 * Adaptive Platform APIs.
 */
#ifndef ARA_CORE_RESULT_H_
#define ARA_CORE_RESULT_H_

#include <exception>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "ara/core/error_code.h"

namespace ara::core {

namespace detail {
template <typename E> [[noreturn]] void ThrowError(E const &e) {
#ifndef ARA_NO_EXCEPTIONS
  if constexpr (std::is_same_v<E, ErrorCode>) {
    e.ThrowAsException();
  } else {
    throw e;
  }
#else
  (void)e;
  std::terminate();
#endif
}

template <typename T> struct IsResult : std::false_type {};
} // namespace detail

template <typename T, typename E = ErrorCode> class Result;
template <typename T, typename E> struct detail::IsResult<Result<T, E>> : std::true_type {};

// ---------------------------------------------------------------------------
// Primary template: Result containing a value or an error
// ---------------------------------------------------------------------------
template <typename T, typename E> class Result final {
  static_assert(!std::is_void_v<T>, "use the Result<void, E> specialization");
  std::variant<T, E> mData;

  template <typename... Args>
  Result(std::in_place_index_t<0> i, Args &&...args) : mData(i, std::forward<Args>(args)...) {}
  template <typename... Args>
  Result(std::in_place_index_t<1> i, Args &&...args) : mData(i, std::forward<Args>(args)...) {}

public:
  using value_type = T;
  using error_type = E;

  // SWS_CORE_00721 / 00722 - from value (implicit)
  Result(T const &t) : mData(std::in_place_index<0>, t) {}
  Result(T &&t) : mData(std::in_place_index<0>, std::move(t)) {}
  // SWS_CORE_00723 / 00724 - from error (explicit)
  explicit Result(E const &e) : mData(std::in_place_index<1>, e) {}
  explicit Result(E &&e) : mData(std::in_place_index<1>, std::move(e)) {}

  Result(Result const &) = default;
  Result(Result &&) noexcept(std::is_nothrow_move_constructible_v<T> &&
                             std::is_nothrow_move_constructible_v<E>) = default;
  Result &operator=(Result const &) = default;
  Result &operator=(Result &&) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                        std::is_nothrow_move_assignable_v<T> &&
                                        std::is_nothrow_move_constructible_v<E> &&
                                        std::is_nothrow_move_assignable_v<E>) = default;
  ~Result() = default;

  // SWS_CORE_00731 ff.
  static Result FromValue(T const &t) { return Result(std::in_place_index<0>, t); }
  static Result FromValue(T &&t) { return Result(std::in_place_index<0>, std::move(t)); }
  template <typename... Args> static Result FromValue(Args &&...args) {
    return Result(std::in_place_index<0>, T{std::forward<Args>(args)...});
  }
  static Result FromError(E const &e) { return Result(std::in_place_index<1>, e); }
  static Result FromError(E &&e) { return Result(std::in_place_index<1>, std::move(e)); }
  template <typename... Args> static Result FromError(Args &&...args) {
    return Result(std::in_place_index<1>, E{std::forward<Args>(args)...});
  }

  // SWS_CORE_00743 / 00744 / 00745
  template <typename... Args> void EmplaceValue(Args &&...args) {
    mData.template emplace<0>(std::forward<Args>(args)...);
  }
  template <typename... Args> void EmplaceError(Args &&...args) {
    mData.template emplace<1>(std::forward<Args>(args)...);
  }
  void Swap(Result &other) { mData.swap(other.mData); }

  // SWS_CORE_00751 / 00752
  bool HasValue() const noexcept { return mData.index() == 0; }
  explicit operator bool() const noexcept { return HasValue(); }

  // SWS_CORE_00753 ff.
  T const &operator*() const & { return std::get<0>(mData); }
  T &&operator*() && { return std::get<0>(std::move(mData)); }
  T const *operator->() const { return &std::get<0>(mData); }
  T const &Value() const & { return std::get<0>(mData); }
  T &&Value() && { return std::get<0>(std::move(mData)); }
  E const &Error() const & { return std::get<1>(mData); }
  E &&Error() && { return std::get<1>(std::move(mData)); }

  std::optional<T> Ok() const & {
    return HasValue() ? std::optional<T>(Value()) : std::nullopt;
  }
  std::optional<T> Ok() && {
    return HasValue() ? std::optional<T>(std::get<0>(std::move(mData))) : std::nullopt;
  }
  std::optional<E> Err() const & {
    return HasValue() ? std::nullopt : std::optional<E>(Error());
  }
  std::optional<E> Err() && {
    return HasValue() ? std::nullopt : std::optional<E>(std::get<1>(std::move(mData)));
  }

  template <typename U> T ValueOr(U &&defaultValue) const & {
    return HasValue() ? Value() : static_cast<T>(std::forward<U>(defaultValue));
  }
  template <typename U> T ValueOr(U &&defaultValue) && {
    return HasValue() ? std::get<0>(std::move(mData)) : static_cast<T>(std::forward<U>(defaultValue));
  }
  template <typename G> E ErrorOr(G &&defaultError) const {
    return HasValue() ? static_cast<E>(std::forward<G>(defaultError)) : Error();
  }
  template <typename G> bool CheckError(G &&error) const {
    return !HasValue() && Error() == static_cast<E>(std::forward<G>(error));
  }

  T const &ValueOrThrow() const & noexcept(false) {
    if (!HasValue()) {
      detail::ThrowError(Error());
    }
    return Value();
  }
  T &&ValueOrThrow() && noexcept(false) {
    if (!HasValue()) {
      detail::ThrowError(Error());
    }
    return std::get<0>(std::move(mData));
  }

  /// Value, or the result of f(error) when *this holds an error.
  template <typename F> T Resolve(F &&f) const {
    return HasValue() ? Value() : static_cast<T>(std::forward<F>(f)(Error()));
  }

  /// Monadic chaining: f(value) returns either a Result<U,E> or a plain U.
  template <typename F> auto Bind(F &&f) const {
    using R = std::invoke_result_t<F, T const &>;
    if constexpr (detail::IsResult<R>::value) {
      return HasValue() ? std::forward<F>(f)(Value()) : R(Error());
    } else if constexpr (std::is_void_v<R>) {
      if (HasValue()) {
        std::forward<F>(f)(Value());
        return Result<void, E>();
      }
      return Result<void, E>(Error());
    } else {
      return HasValue() ? Result<R, E>(std::forward<F>(f)(Value())) : Result<R, E>(Error());
    }
  }
};

// ---------------------------------------------------------------------------
// Specialization for void
// ---------------------------------------------------------------------------
template <typename E> class Result<void, E> final {
  std::optional<E> mError;

public:
  using value_type = void;
  using error_type = E;

  Result() noexcept : mError(std::nullopt) {}
  explicit Result(E const &e) : mError(e) {}
  explicit Result(E &&e) : mError(std::move(e)) {}
  Result(Result const &) = default;
  Result(Result &&) noexcept(std::is_nothrow_move_constructible_v<E>) = default;
  Result &operator=(Result const &) = default;
  Result &operator=(Result &&) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                        std::is_nothrow_move_assignable_v<E>) = default;
  ~Result() = default;

  static Result FromValue() { return Result(); }
  static Result FromError(E const &e) { return Result(e); }
  static Result FromError(E &&e) { return Result(std::move(e)); }
  template <typename... Args> static Result FromError(Args &&...args) {
    return Result(E{std::forward<Args>(args)...});
  }

  void EmplaceValue() noexcept { mError.reset(); }
  template <typename... Args> void EmplaceError(Args &&...args) {
    mError.emplace(std::forward<Args>(args)...);
  }
  void Swap(Result &other) { mError.swap(other.mError); }

  bool HasValue() const noexcept { return !mError.has_value(); }
  explicit operator bool() const noexcept { return HasValue(); }

  void operator*() const {}
  void Value() const {}
  E const &Error() const & { return *mError; }
  E &&Error() && { return std::move(*mError); }
  std::optional<E> Err() const { return mError; }

  template <typename G> E ErrorOr(G &&defaultError) const {
    return HasValue() ? static_cast<E>(std::forward<G>(defaultError)) : Error();
  }
  template <typename G> bool CheckError(G &&error) const {
    return !HasValue() && Error() == static_cast<E>(std::forward<G>(error));
  }

  void ValueOrThrow() const noexcept(false) {
    if (!HasValue()) {
      detail::ThrowError(Error());
    }
  }
  template <typename F> void Resolve(F &&f) const {
    if (!HasValue()) {
      std::forward<F>(f)(Error());
    }
  }
  template <typename F> auto Bind(F &&f) const {
    using R = std::invoke_result_t<F>;
    if constexpr (detail::IsResult<R>::value) {
      return HasValue() ? std::forward<F>(f)() : R(Error());
    } else if constexpr (std::is_void_v<R>) {
      if (HasValue()) {
        std::forward<F>(f)();
        return Result<void, E>();
      }
      return Result<void, E>(Error());
    } else {
      return HasValue() ? Result<R, E>(std::forward<F>(f)()) : Result<R, E>(Error());
    }
  }
};

// ---------------------------------------------------------------------------
// Comparison and swap  (SWS_CORE_00781 ff.)
// ---------------------------------------------------------------------------
template <typename T, typename E>
bool operator==(Result<T, E> const &lhs, Result<T, E> const &rhs) {
  if (lhs.HasValue() != rhs.HasValue()) {
    return false;
  }
  if (lhs.HasValue()) {
    if constexpr (std::is_void_v<T>) {
      return true;
    } else {
      return lhs.Value() == rhs.Value();
    }
  }
  return lhs.Error() == rhs.Error();
}
template <typename T, typename E>
bool operator!=(Result<T, E> const &lhs, Result<T, E> const &rhs) {
  return !(lhs == rhs);
}
template <typename T, typename E, typename = std::enable_if_t<!std::is_void_v<T>>>
bool operator==(Result<T, E> const &lhs, T const &value) {
  return lhs.HasValue() && lhs.Value() == value;
}
template <typename T, typename E, typename = std::enable_if_t<!std::is_void_v<T>>>
bool operator==(T const &value, Result<T, E> const &rhs) {
  return rhs == value;
}
template <typename T, typename E, typename = std::enable_if_t<!std::is_void_v<T>>>
bool operator!=(Result<T, E> const &lhs, T const &value) {
  return !(lhs == value);
}
template <typename T, typename E, typename = std::enable_if_t<!std::is_void_v<T>>>
bool operator!=(T const &value, Result<T, E> const &rhs) {
  return !(rhs == value);
}
template <typename T, typename E>
bool operator==(Result<T, E> const &lhs, E const &error) {
  return !lhs.HasValue() && lhs.Error() == error;
}
template <typename T, typename E>
bool operator==(E const &error, Result<T, E> const &rhs) {
  return rhs == error;
}
template <typename T, typename E>
bool operator!=(Result<T, E> const &lhs, E const &error) {
  return !(lhs == error);
}
template <typename T, typename E>
bool operator!=(E const &error, Result<T, E> const &rhs) {
  return !(rhs == error);
}
template <typename T, typename E> void swap(Result<T, E> &lhs, Result<T, E> &rhs) {
  lhs.Swap(rhs);
}

} // namespace ara::core

#endif // ARA_CORE_RESULT_H_
