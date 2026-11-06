/**
 * @file log_stream.h
 * @brief ara::log::LogStream and the Hex/Bin formatting helpers (SWS_LOG_00xxx).
 */
#ifndef ARA_LOG_LOG_STREAM_H_
#define ARA_LOG_LOG_STREAM_H_

#include <cstdint>
#include <string>
#include <string_view>

#include <chrono>
#include <type_traits>
#include <utility>

#include "ara/core/byte.h"
#include "ara/core/error_code.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/span.h"
#include "ara/log/logging_types.h"

namespace ara::log {

class Logger;

/// Fixed-width hexadecimal rendering of an integer ("0x" + 2*sizeof(T) digits).
template <typename T> struct HexFormat { T value; };
/// Fixed-width binary rendering of an integer ("0b" + 8*sizeof(T) digits).
template <typename T> struct BinFormat { T value; };

using LogHex8 = HexFormat<std::uint8_t>;
using LogHex16 = HexFormat<std::uint16_t>;
using LogHex32 = HexFormat<std::uint32_t>;
using LogHex64 = HexFormat<std::uint64_t>;
using LogBin8 = BinFormat<std::uint8_t>;
using LogBin16 = BinFormat<std::uint16_t>;
using LogBin32 = BinFormat<std::uint32_t>;
using LogBin64 = BinFormat<std::uint64_t>;

/// A value together with a formatting hint; create with Arg().
template <typename T> struct Argument {
  T value;
  Format format;
};

/// Attaches a formatting hint: stream << Arg(x, Hex(4)).
template <typename T>
Argument<std::decay_t<T>> Arg(T &&value, Format format = Dflt()) {
  return {std::forward<T>(value), format};
}

namespace detail {
std::string ToHex(std::uint64_t v, unsigned digits);
std::string ToBin(std::uint64_t v, unsigned bits);
std::string FormatInteger(std::uint64_t magnitude, bool negative, unsigned bytes, Format f);
std::string FormatFloating(double v, Format f);
std::string FormatPointer(void const *p);
std::string FormatErrorCode(ara::core::ErrorCode const &ec);
void Emit(LogLevel level, std::string const &ctx, std::string message, std::string tag,
          std::string file, int line);

template <typename T> constexpr std::uint64_t ToU64(T v) noexcept {
  if constexpr (std::is_same_v<T, std::uint64_t>) {
    return v;
  } else {
    return static_cast<std::uint64_t>(v);
  }
}
} // namespace detail

/**
 * Collects one log record and emits it on Flush() or destruction.
 * A stream created for a disabled level is inert: operator<< does nothing and
 * no formatting cost is paid beyond the call itself.
 */
class LogStream final {
public:
  LogStream(LogStream &&other) noexcept;
  LogStream &operator=(LogStream &&other) noexcept;
  LogStream(LogStream const &) = delete;
  LogStream &operator=(LogStream const &) = delete;
  ~LogStream();

  /// Emits the record now; subsequent insertions start a new record.
  void Flush() noexcept;

  LogStream &operator<<(bool v);
  LogStream &operator<<(char v);
  LogStream &operator<<(char const *v);
  LogStream &operator<<(std::string const &v);
  LogStream &operator<<(std::string_view v);
  LogStream &operator<<(std::int8_t v) { return Append(std::to_string(static_cast<int>(v))); }
  LogStream &operator<<(std::uint8_t v) { return Append(std::to_string(static_cast<unsigned>(v))); }
  LogStream &operator<<(short v) { return Append(std::to_string(v)); }
  LogStream &operator<<(unsigned short v) { return Append(std::to_string(v)); }
  LogStream &operator<<(int v) { return Append(std::to_string(v)); }
  LogStream &operator<<(unsigned v) { return Append(std::to_string(v)); }
  LogStream &operator<<(long v) { return Append(std::to_string(v)); }
  LogStream &operator<<(unsigned long v) { return Append(std::to_string(v)); }
  LogStream &operator<<(long long v) { return Append(std::to_string(v)); }
  LogStream &operator<<(unsigned long long v) { return Append(std::to_string(v)); }
  LogStream &operator<<(float v);
  LogStream &operator<<(double v);
  LogStream &operator<<(LogLevel v) { return Append(ToString(v)); }

  template <typename T> LogStream &operator<<(HexFormat<T> h) {
    if (!enabled_) return *this;
    return Append("0x" + detail::ToHex(static_cast<std::uint64_t>(h.value) &
                                           MaskFor(sizeof(T)),
                                       static_cast<unsigned>(sizeof(T) * 2)));
  }
  template <typename T> LogStream &operator<<(BinFormat<T> b) {
    if (!enabled_) return *this;
    return Append("0b" + detail::ToBin(static_cast<std::uint64_t>(b.value) & MaskFor(sizeof(T)),
                                       static_cast<unsigned>(sizeof(T) * 8)));
  }

  /// Attaches the source location to the record.
  LogStream &WithLocation(std::string_view file, int line) noexcept;
  /// Attaches a free-form tag to the record.
  LogStream &WithTag(std::string_view tag) noexcept;

  LogStream &operator<<(void const *p);
  LogStream &operator<<(ara::core::ErrorCode const &ec);
  LogStream &operator<<(ara::core::InstanceSpecifier const &is);
  /// A byte span is rendered as space-separated two-digit lowercase hex values ("0a ff 10").
  LogStream &operator<<(ara::core::Span<const ara::core::Byte> bytes);
  /// Durations are rendered as an integer number of nanoseconds followed by "ns".
  template <typename Rep, typename Period>
  LogStream &operator<<(std::chrono::duration<Rep, Period> d) {
    if (!enabled_) return *this;
    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
    return Append(std::to_string(ns) + "ns");
  }

  template <typename T> LogStream &operator<<(Argument<T> const &a) {
    if (!enabled_) return *this;
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                  !(std::is_integral_v<T> || std::is_floating_point_v<T>)) {
      return *this << a.value;
    } else if constexpr (std::is_integral_v<T>) {
      bool negative = false;
      std::uint64_t magnitude = detail::ToU64(a.value);
      if constexpr (std::is_signed_v<T>) {
        if (a.value < 0) {
          negative = true;
          magnitude = ~magnitude + 1U;
        }
      }
      return Append(detail::FormatInteger(magnitude, negative, sizeof(T), a.format));
    } else {
      return Append(detail::FormatFloating(static_cast<double>(a.value), a.format));
    }
  }

  bool IsEnabled() const noexcept { return enabled_; }

private:
  friend class Logger;
  LogStream(Logger const *logger, LogLevel level, bool enabled) noexcept;
  LogStream &Append(std::string_view s);
  static constexpr std::uint64_t MaskFor(std::size_t bytes) noexcept {
    return bytes >= 8 ? ~std::uint64_t{0} : ((std::uint64_t{1} << (bytes * 8)) - 1);
  }

  Logger const *logger_;
  LogLevel level_;
  bool enabled_;
  std::string buffer_;
  std::string tag_;
  std::string file_;
  int line_{0};
};

} // namespace ara::log

#endif // ARA_LOG_LOG_STREAM_H_
