/**
 * @file diag_facade.h
 * @brief AUTOSAR-shaped facade over DiagnosticServer: Conversation, GenericUDSService, GenericRoutine,
 *        GenericDataIdentifier and Monitor.
 *
 * Class and method names follow the SWS Diagnostics table of contents (Offer / StopOffer / HandleMessage /
 * Start / Stop / RequestResults / ReportMonitorAction).  Signatures are pragmatic: they use ara::core::Result
 * and plain byte vectors instead of the SWS Future / OperationOutput / MetaInfo types, and the numeric values of
 * the enumerators are implementation choices (see doc/spec-conformance/diag.md).
 */
#ifndef ARA_DIAG_DIAG_FACADE_H_
#define ARA_DIAG_DIAG_FACADE_H_

#include <cstdint>
#include <optional>

#include "ara/diag/diagnostic_server.h"

namespace ara::diag {

using ConversationIdentifierType = std::uint32_t;

/// One tester connection to the server (SWS: Conversation).  Requests are routed through the shared engine.
class Conversation {
public:
  Conversation(DiagnosticServer &server, ConversationIdentifierType id) noexcept : server_(server), id_(id) {}
  ConversationIdentifierType GetConversationIdentifier() const noexcept { return id_; }
  /// Processes a request; nullopt = no response (suppressPositiveResponse / functional "not supported").
  std::optional<ByteVector> Request(ByteVector const &request, Addressing addressing = Addressing::kPhysical) {
    return server_.Dispatch(request, addressing);
  }
  uds::Session GetActiveSession() { return server_.CurrentSession(); }
  std::uint8_t GetActiveSecurityLevel() { return server_.CurrentSecurityLevel(); }
  CommControlState GetCommunicationState() { return server_.CommControl(); }

private:
  DiagnosticServer &server_;
  ConversationIdentifierType id_;
};

/// Application-implemented UDS service (SWS: GenericUDSService).  Offer() hooks HandleMessage into the server.
class GenericUDSService {
public:
  GenericUDSService(DiagnosticServer &server, std::uint8_t sid, std::uint8_t sessions = kAllSessions) noexcept
      : server_(server), sid_(sid), sessions_(sessions) {}
  GenericUDSService(GenericUDSService const &) = delete;
  GenericUDSService &operator=(GenericUDSService const &) = delete;
  virtual ~GenericUDSService() { (void)StopOffer(); }

  ara::core::Result<void> Offer();
  ara::core::Result<void> StopOffer();
  /// @p requestAfterSid is the request without the SID; return the response bytes after SID+0x40 or an NRC.
  virtual ServiceResult HandleMessage(ByteVector const &requestAfterSid) = 0;

private:
  DiagnosticServer &server_;
  std::uint8_t sid_;
  std::uint8_t sessions_;
  bool offered_{false};
};

/// Application-implemented routine (SWS: GenericRoutine).
class GenericRoutine {
public:
  GenericRoutine(DiagnosticServer &server, std::uint16_t routineId, std::uint8_t sessions = kAllSessions,
                 std::uint8_t securityLevel = 0) noexcept
      : server_(server), id_(routineId), sessions_(sessions), level_(securityLevel) {}
  GenericRoutine(GenericRoutine const &) = delete;
  GenericRoutine &operator=(GenericRoutine const &) = delete;
  virtual ~GenericRoutine() { (void)StopOffer(); }

  ara::core::Result<void> Offer();
  ara::core::Result<void> StopOffer();
  /// Default implementations answer "sub-function not supported".
  virtual ara::core::Result<ByteVector, uds::Nrc> Start(ByteVector const &option);
  virtual ara::core::Result<ByteVector, uds::Nrc> Stop(ByteVector const &option);
  virtual ara::core::Result<ByteVector, uds::Nrc> RequestResults(ByteVector const &option);

private:
  DiagnosticServer &server_;
  std::uint16_t id_;
  std::uint8_t sessions_;
  std::uint8_t level_;
  bool offered_{false};
};

/// Application-implemented data identifier (SWS: GenericDataIdentifier).
class GenericDataIdentifier {
public:
  GenericDataIdentifier(DiagnosticServer &server, std::uint16_t did) noexcept : server_(server), did_(did) {}
  GenericDataIdentifier(GenericDataIdentifier const &) = delete;
  GenericDataIdentifier &operator=(GenericDataIdentifier const &) = delete;
  virtual ~GenericDataIdentifier() { (void)StopOffer(); }

  ara::core::Result<void> Offer();
  ara::core::Result<void> StopOffer();
  virtual ara::core::Result<ByteVector, uds::Nrc> Read() = 0;
  /// Return true when the DID is writable; Write() is then called by 0x2E.
  virtual bool IsWritable() const { return false; }
  virtual ara::core::Result<void, uds::Nrc> Write(ByteVector const &) { return ara::core::Result<void, uds::Nrc>::FromError(uds::Nrc::kRequestOutOfRange); }

protected:
  std::size_t writeLength_{0};
  std::uint8_t writeSecurityLevel_{0};

private:
  DiagnosticServer &server_;
  std::uint16_t did_;
  bool offered_{false};
};

/// Results a monitor reports to its DTC (SWS: MonitorAction).  Enumerator values are implementation choices.
enum class MonitorAction : std::uint8_t {
  kPassed = 0,
  kFailed = 1,
  kPrepassed = 2,
  kPrefailed = 3,
  kFreezeDebouncing = 4,
  kResetDebouncing = 5,
};

/// A monitor reports test results for one DTC and debounces pre-passed / pre-failed (SWS: Monitor).
class Monitor {
public:
  struct Debounce {
    int failedThreshold{3};  ///< consecutive kPrefailed that turn into a failure
    int passedThreshold{3};  ///< consecutive kPrepassed that turn into a pass
  };
  Monitor(DiagnosticServer &server, std::uint32_t dtc) noexcept : Monitor(server, dtc, Debounce{}) {}
  Monitor(DiagnosticServer &server, std::uint32_t dtc, Debounce cfg) noexcept : server_(server), dtc_(dtc), cfg_(cfg) {}

  ara::core::Result<void> Offer();
  ara::core::Result<void> StopOffer();
  ara::core::Result<void> ReportMonitorAction(MonitorAction action);
  /// FaultDetectionCounter, -128 (passed) .. +127 (failed), as reported through 0x19 in ISO 14229-1.
  std::int8_t GetFaultDetectionCounter() const noexcept;

private:
  DiagnosticServer &server_;
  std::uint32_t dtc_;
  Debounce cfg_;
  bool offered_{false};
  bool frozen_{false};
  int counter_{0}; ///< + prefailed, - prepassed
};

} // namespace ara::diag

#endif // ARA_DIAG_DIAG_FACADE_H_
