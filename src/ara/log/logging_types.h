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
