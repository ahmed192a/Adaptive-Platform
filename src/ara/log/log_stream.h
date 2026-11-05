/**
 * @file log_stream.h
 * @brief ara::log::LogStream and the Hex/Bin formatting helpers (SWS_LOG_00xxx).
 */
#ifndef ARA_LOG_LOG_STREAM_H_
#define ARA_LOG_LOG_STREAM_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "ara/log/logging_types.h"

namespace ara::log {

class Logger;

/// Wrapper requesting hexadecimal rendering of an integer.
template <typename T> struct HexFormat { T value; };
/// Wrapper requesting binary rendering of an integer.
template <typename T> struct BinFormat { T value; };

template <typename T> constexpr HexFormat<T> Hex(T v) noexcept {
  static_assert(std::is_integral_v<T>, "Hex needs an integer");
  return {v};
}
template <typename T> constexpr BinFormat<T> Bin(T v) noexcept {
  static_assert(std::is_integral_v<T>, "Bin needs an integer");
  return {v};
}

namespace detail {
std::string ToHex(std::uint64_t v, unsigned digits);
std::string ToBin(std::uint64_t v, unsigned bits);
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
};

} // namespace ara::log

#endif // ARA_LOG_LOG_STREAM_H_
