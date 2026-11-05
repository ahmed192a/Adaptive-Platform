/**
 * @file diagnostic_server.h
 * @brief Transport-independent UDS request dispatcher.
 *
 * The server turns a request PDU into a response PDU.  It implements the
 * session layer (0x10, 0x3E, S3 timeout), 0x22/0x2E on registered data
 * identifiers and 0x11, and lets applications add services.  Transport (DoIP /
 * ISO-TP) is out of scope and is expected to call Dispatch().
 *
 * This is the engine below the SWS ara::diag service interfaces; the
 * AUTOSAR-shaped facade (Conversation, GenericUDSService, DataIdentifier ...)
 * is planned on top of it.
 */
#ifndef ARA_DIAG_DIAGNOSTIC_SERVER_H_
#define ARA_DIAG_DIAGNOSTIC_SERVER_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

#include "ara/core/result.h"
#include "ara/diag/diag_error_domain.h"
#include "ara/diag/uds.h"

namespace ara::diag {

/// Bitmask of uds::SessionBit values; kAllSessions allows every session.
inline constexpr std::uint8_t kAllSessions = 0x07;

/// A handler returns the response payload (after SID+0x40) or an NRC.
using ServiceResult = ara::core::Result<ByteVector, uds::Nrc>;
using ServiceHandler = std::function<ServiceResult(ByteVector const &requestAfterSid)>;

struct DataIdentifier {
  /// Returns the value bytes, or an NRC.
  std::function<ara::core::Result<ByteVector, uds::Nrc>()> read;
  /// Optional.  Absent = read-only DID.
  std::function<ara::core::Result<void, uds::Nrc>(ByteVector const &)> write;
  std::uint8_t readSessions{kAllSessions};
  std::uint8_t writeSessions{uds::SessionBit(uds::Session::kExtended)};
  /// Expected length of a write payload; 0 = any.
  std::size_t writeLength{0};
};

class DiagnosticServer {
public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  using ResetHandler = std::function<bool(std::uint8_t resetType)>;

  struct Config {
    std::chrono::milliseconds s3Timeout{5000};
    std::chrono::milliseconds p2Max{50};   ///< reported in the 0x10 response
    std::chrono::milliseconds p2StarMax{5000};
    Clock clock; ///< defaults to steady_clock::now
  };

  DiagnosticServer();
  explicit DiagnosticServer(Config cfg);

  ara::core::Result<void> RegisterDataIdentifier(std::uint16_t did, DataIdentifier id);
  ara::core::Result<void> RegisterService(std::uint8_t sid, std::uint8_t allowedSessions,
                                          ServiceHandler handler);
  void SetResetHandler(ResetHandler h);

  /// Processes one request.  Returns nullopt when no response must be sent
  /// (suppressPositiveResponse).  Never throws.
  std::optional<ByteVector> Dispatch(ByteVector const &request);

  uds::Session CurrentSession();

private:
  using TimePoint = std::chrono::steady_clock::time_point;
  struct Service {
    std::uint8_t sessions;
    ServiceHandler handler;
  };
  void ExpireSessionLocked(TimePoint now);
  std::optional<ByteVector> HandleSessionControl(ByteVector const &req);
  std::optional<ByteVector> HandleTesterPresent(ByteVector const &req);
  std::optional<ByteVector> HandleEcuReset(ByteVector const &req);
  std::optional<ByteVector> HandleRead(ByteVector const &req);
  std::optional<ByteVector> HandleWrite(ByteVector const &req);
  bool InSession(std::uint8_t mask) const { return (mask & uds::SessionBit(session_)) != 0; }

  Config cfg_;
  std::mutex mutex_; ///< guards registries and session state
  uds::Session session_{uds::Session::kDefault};
  TimePoint lastActivity_;
  std::map<std::uint16_t, DataIdentifier> dids_;
  std::map<std::uint8_t, Service> services_;
  ResetHandler reset_;
};

} // namespace ara::diag

#endif // ARA_DIAG_DIAGNOSTIC_SERVER_H_
