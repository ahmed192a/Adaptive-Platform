/**
 * @file uds.h
 * @brief UDS (ISO 14229-1) constants used by the diagnostic server.
 */
#ifndef ARA_DIAG_UDS_H_
#define ARA_DIAG_UDS_H_

#include <cstdint>
#include <vector>

namespace ara::diag {

using ByteVector = std::vector<std::uint8_t>;

namespace uds {
inline constexpr std::uint8_t kNegativeResponseSid = 0x7F;
inline constexpr std::uint8_t kPositiveResponseOffset = 0x40;
inline constexpr std::uint8_t kSuppressPositiveResponseBit = 0x80;

enum Sid : std::uint8_t {
  kDiagnosticSessionControl = 0x10,
  kEcuReset = 0x11,
  kClearDiagnosticInformation = 0x14,
  kReadDtcInformation = 0x19,
  kReadDataByIdentifier = 0x22,
  kSecurityAccess = 0x27,
  kWriteDataByIdentifier = 0x2E,
  kRoutineControl = 0x31,
  kTesterPresent = 0x3E,
  kControlDtcSetting = 0x85,
};

enum class Session : std::uint8_t { kDefault = 0x01, kProgramming = 0x02, kExtended = 0x03 };

/// Negative response codes (ISO 14229-1 Annex A).
enum class Nrc : std::uint8_t {
  kGeneralReject = 0x10,
  kServiceNotSupported = 0x11,
  kSubFunctionNotSupported = 0x12,
  kIncorrectMessageLengthOrInvalidFormat = 0x13,
  kResponseTooLong = 0x14,
  kConditionsNotCorrect = 0x22,
  kRequestSequenceError = 0x24,
  kRequestOutOfRange = 0x31,
  kSecurityAccessDenied = 0x33,
  kInvalidKey = 0x35,
  kExceededNumberOfAttempts = 0x36,
  kRequiredTimeDelayNotExpired = 0x37,
  kGeneralProgrammingFailure = 0x72,
  kResponsePending = 0x78,
  kSubFunctionNotSupportedInActiveSession = 0x7E,
  kServiceNotSupportedInActiveSession = 0x7F,
};

inline ByteVector NegativeResponse(std::uint8_t sid, Nrc nrc) {
  return {kNegativeResponseSid, sid, static_cast<std::uint8_t>(nrc)};
}

constexpr std::uint8_t SessionBit(Session s) noexcept {
  return static_cast<std::uint8_t>(1U << (static_cast<unsigned>(s) - 1U));
}
} // namespace uds

} // namespace ara::diag

#endif // ARA_DIAG_UDS_H_
