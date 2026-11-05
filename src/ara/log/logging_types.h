/**
 * @file logging_types.h
 * @brief ara::log basic types (SWS_LOG): LogLevel, LogMode, ClientState and helpers.
 */
#ifndef ARA_LOG_LOGGING_TYPES_H_
#define ARA_LOG_LOGGING_TYPES_H_

#include <cstdint>
#include <type_traits>

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"
#include "ara/core/string.h"

namespace ara::log {

enum class LogLevel : std::uint8_t {
  kOff = 0x00,
  kFatal = 0x01,
  kError = 0x02,
  kWarn = 0x03,
  kInfo = 0x04,
  kDebug = 0x05,
  kVerbose = 0x06
};

/// Bit flags selecting the sinks.
enum class LogMode : std::uint8_t { kRemote = 0x01, kFile = 0x02, kConsole = 0x04 };

constexpr LogMode operator|(LogMode a, LogMode b) noexcept {
  return static_cast<LogMode>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr LogMode operator&(LogMode a, LogMode b) noexcept {
  return static_cast<LogMode>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
constexpr bool HasMode(LogMode set, LogMode flag) noexcept {
  return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(flag)) != 0;
}

/// Format specifiers for log arguments (SWS_LOG "Fmt", values as published for R25-11 tooling).
enum class Fmt : std::uint16_t {
  kDefault = 0,
  kDec = 1,
  kOct = 2,
  kHex = 3,
  kBin = 4,
  kDecFloat = 5,
  kEngFloat = 6,
  kHexFloat = 7,
  kAutoFloat = 8
};

/// Formatting hint attached to an argument via ara::log::Arg(value, Hex(4)).
/// `precision` is the minimum number of digits for integers and the number of
/// fractional digits for floating point values (0 = implementation default).
struct Format {
  Fmt fmt{Fmt::kDefault};
  std::uint16_t precision{0};
};

constexpr Format Dflt() noexcept { return {Fmt::kDefault, 0}; }
constexpr Format Dec() noexcept { return {Fmt::kDec, 0}; }
constexpr Format Dec(std::uint16_t digits) noexcept { return {Fmt::kDec, digits}; }
constexpr Format Oct() noexcept { return {Fmt::kOct, 0}; }
constexpr Format Oct(std::uint16_t digits) noexcept { return {Fmt::kOct, digits}; }
constexpr Format Hex() noexcept { return {Fmt::kHex, 0}; }
constexpr Format Hex(std::uint16_t digits) noexcept { return {Fmt::kHex, digits}; }
constexpr Format Bin() noexcept { return {Fmt::kBin, 0}; }
constexpr Format Bin(std::uint16_t digits) noexcept { return {Fmt::kBin, digits}; }
/// "...Max" variants request the maximal precision of a double (17 significant digits).
inline constexpr std::uint16_t kMaxFloatPrecision = 17;
constexpr Format DecFloat() noexcept { return {Fmt::kDecFloat, 0}; }
constexpr Format DecFloat(std::uint16_t p) noexcept { return {Fmt::kDecFloat, p}; }
constexpr Format DecFloatMax() noexcept { return {Fmt::kDecFloat, kMaxFloatPrecision}; }
constexpr Format EngFloat() noexcept { return {Fmt::kEngFloat, 0}; }
constexpr Format EngFloat(std::uint16_t p) noexcept { return {Fmt::kEngFloat, p}; }
constexpr Format EngFloatMax() noexcept { return {Fmt::kEngFloat, kMaxFloatPrecision}; }
constexpr Format HexFloat() noexcept { return {Fmt::kHexFloat, 0}; }
constexpr Format HexFloat(std::uint16_t p) noexcept { return {Fmt::kHexFloat, p}; }
constexpr Format HexFloatMax() noexcept { return {Fmt::kHexFloat, kMaxFloatPrecision}; }
constexpr Format AutoFloat() noexcept { return {Fmt::kAutoFloat, 0}; }
constexpr Format AutoFloat(std::uint16_t p) noexcept { return {Fmt::kAutoFloat, p}; }
constexpr Format AutoFloatMax() noexcept { return {Fmt::kAutoFloat, kMaxFloatPrecision}; }

enum class ClientState : std::int8_t { kUnknown = -1, kNotConnected = 0, kConnected = 1 };

/// Textual form used by the console / file sinks ("FATL", "ERRO", ...).
constexpr char const *ToString(LogLevel l) noexcept {
  switch (l) {
  case LogLevel::kOff: return "OFF ";
  case LogLevel::kFatal: return "FATL";
  case LogLevel::kError: return "ERRO";
  case LogLevel::kWarn: return "WARN";
  case LogLevel::kInfo: return "INFO";
  case LogLevel::kDebug: return "DEBG";
  case LogLevel::kVerbose: return "VERB";
  }
  return "????";
}

// --------------------------------------------------------------------------
// Error domain
// --------------------------------------------------------------------------
enum class LogErrc : ara::core::ErrorDomain::CodeType {
  kInvalidArgument = 1,
  kNotInitialized = 2,
  kSinkFailure = 3,
};

class LogException : public ara::core::Exception {
public:
  explicit LogException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class LogErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000403; // implementation choice (see ROADMAP)
  constexpr LogErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Log"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr LogErrorDomain kLogErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetLogDomain() noexcept {
  return internal::kLogErrorDomain;
}
constexpr ara::core::ErrorCode MakeErrorCode(LogErrc code,
                                             ara::core::ErrorDomain::SupportDataType data = 0,
                                             char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetLogDomain(),
                              data, message);
}

} // namespace ara::log

#endif // ARA_LOG_LOGGING_TYPES_H_
