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
 * AUTOSAR-shaped facade (Conversation, GenericUDSService, GenericRoutine,
 * GenericDataIdentifier, Monitor) lives in diag_facade.h on top of it.
 */
#ifndef ARA_DIAG_DIAGNOSTIC_SERVER_H_
#define ARA_DIAG_DIAGNOSTIC_SERVER_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

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
  /// Security level (SecurityAccess sub-function pair) required for writing; 0 = none.
  std::uint8_t writeSecurityLevel{0};
};

/// Routine handlers for RoutineControl (0x31).  Missing handler = sub-function not supported.
struct Routine {
  using Handler = std::function<ara::core::Result<ByteVector, uds::Nrc>(ByteVector const &option)>;
  Handler start;
  Handler stop;
  Handler requestResults;
  std::uint8_t sessions{kAllSessions};
  std::uint8_t securityLevel{0}; ///< required level, 0 = none
};

/// Seed/key callbacks for SecurityAccess (0x27).
struct SecurityProvider {
  /// Returns the seed for @p level (a non-zero byte string).
  std::function<ara::core::Result<ByteVector, uds::Nrc>(std::uint8_t level)> seed;
  /// Returns true when @p key is the right answer for @p seed at @p level.
  std::function<bool(std::uint8_t level, ByteVector const &seed, ByteVector const &key)> validateKey;
  std::uint8_t maxAttempts{3};
  std::chrono::milliseconds lockout{10000};
};

/// DTC status bits (ISO 14229-1 Annex D).
namespace dtc {
inline constexpr std::uint8_t kTestFailed = 0x01;
inline constexpr std::uint8_t kTestFailedThisOperationCycle = 0x02;
inline constexpr std::uint8_t kConfirmed = 0x08;
inline constexpr std::uint8_t kTestFailedSinceLastClear = 0x20;
inline constexpr std::uint8_t kStatusAvailabilityMask = 0x2B; ///< the bits above
} // namespace dtc

/// How a request was addressed.  Functional requests do not get "not supported" negative responses.
enum class Addressing : std::uint8_t { kPhysical, kFunctional };

/// A readable/writable memory window for ReadMemoryByAddress (0x23) / WriteMemoryByAddress (0x3D).
struct MemoryRegion {
  std::uint64_t start{0};
  std::uint64_t size{0};
  std::function<ara::core::Result<ByteVector, uds::Nrc>(std::uint64_t address, std::uint64_t length)> read;
  /// Optional.  Absent = read-only region.
  std::function<ara::core::Result<void, uds::Nrc>(std::uint64_t address, ByteVector const &data)> write;
  std::uint8_t sessions{kAllSessions};
  std::uint8_t securityLevel{0};
};

enum class TransferDirection : std::uint8_t { kDownload, kUpload };

/// Handler for the data-transfer services 0x34 (download), 0x35 (upload), 0x36 and 0x37.
struct TransferHandler {
  /// Called for 0x34/0x35.  Returns the maximum block length (including SID and block counter, >= 3).
  std::function<ara::core::Result<std::uint32_t, uds::Nrc>(TransferDirection dir, std::uint64_t address,
                                                            std::uint64_t size, std::uint8_t dataFormat)>
      request;
  /// Called for 0x36.  Download: @p data is the block, the return value is the (usually empty) response
  /// record.  Upload: @p data is empty and the return value is the block to send.
  std::function<ara::core::Result<ByteVector, uds::Nrc>(std::uint8_t blockCounter, ByteVector const &data)> block;
  /// Called for 0x37 with the optional request record; returns the optional response record.
  std::function<ara::core::Result<ByteVector, uds::Nrc>(ByteVector const &record)> exit;
  std::uint8_t sessions{uds::SessionBit(uds::Session::kProgramming) | uds::SessionBit(uds::Session::kExtended)};
  std::uint8_t securityLevel{0};
};

/// One entry of the DTC event memory.
struct DtcRecord {
  std::uint8_t status{0};
  std::uint8_t occurrences{0}; ///< times the DTC went from "not failed" to "failed" (saturates at 255)
  /// DTCSnapshotRecordNumber -> encoded record [numberOfIdentifiers, (DID hi, DID lo, data...)...].
  std::map<std::uint8_t, ByteVector> snapshots;
  /// DTCExtDataRecordNumber -> record data.  Record 0x01 (occurrence counter) is synthesised.
  std::map<std::uint8_t, ByteVector> extendedData;
};
using DtcMemory = std::map<std::uint32_t, DtcRecord>;

/// Persistence hooks for the event memory (see per_dtc_storage.h for an ara::per backed implementation).
struct DtcStorage {
  std::function<DtcMemory()> load;
  std::function<void(DtcMemory const &)> save;
};

/// State set by CommunicationControl (0x28).
struct CommControlState {
  bool rxEnabled{true};
  bool txEnabled{true};
  std::uint8_t communicationType{0x01};
};

class DiagnosticServer {
public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  using ResetHandler = std::function<bool(std::uint8_t resetType)>;
  using CommControlHandler = std::function<bool(std::uint8_t subFunction, std::uint8_t communicationType)>;
  using ResponseSink = std::function<void(ByteVector const &)>;

  struct Config {
    std::chrono::milliseconds s3Timeout{5000};
    std::chrono::milliseconds p2Max{50};   ///< reported in the 0x10 response
    std::chrono::milliseconds p2StarMax{5000};
    Clock clock; ///< defaults to steady_clock::now
    std::uint8_t powerDownTime{5}; ///< seconds, reported for ECUReset enableRapidPowerShutDown (0x04)
    /// How often a handler that answers NRC 0x78 (responsePending) is polled again.
    unsigned maxPendingRetries{16};
  };

  DiagnosticServer();
  explicit DiagnosticServer(Config cfg);

  ara::core::Result<void> RegisterDataIdentifier(std::uint16_t did, DataIdentifier id);
  ara::core::Result<void> RegisterService(std::uint8_t sid, std::uint8_t allowedSessions,
                                          ServiceHandler handler);
  /// Removal counterparts, used by the facade classes' StopOffer().
  ara::core::Result<void> UnregisterDataIdentifier(std::uint16_t did);
  ara::core::Result<void> UnregisterService(std::uint8_t sid);
  ara::core::Result<void> UnregisterRoutine(std::uint16_t id);
  /// Restricts a built-in service to the sessions in @p allowedSessions (others get NRC 0x7F).
  ara::core::Result<void> RestrictBuiltinService(std::uint8_t sid, std::uint8_t allowedSessions);
  void SetResetHandler(ResetHandler h);
  /// CommunicationControl (0x28).  Without a handler the service answers NRC 0x11.
  void SetCommControlHandler(CommControlHandler h);
  CommControlState CommControl();
  /// Receives the intermediate "7F sid 78" responses sent while a handler keeps answering responsePending.
  void SetResponsePendingSink(ResponseSink s);
  ara::core::Result<void> RegisterMemoryRegion(MemoryRegion r);
  void SetTransferHandler(TransferHandler h);
  /// True while a 0x34/0x35 transfer is open.
  bool TransferActive();
  ara::core::Result<void> RegisterRoutine(std::uint16_t id, Routine r);
  void SetSecurityProvider(SecurityProvider p);
  /// Current unlocked security level (0 = locked).  Reset on every session change.
  std::uint8_t CurrentSecurityLevel();

  /// Event memory: report a DTC test result (ignored while 0x85 switched DTC setting off).
  void ReportDtc(std::uint32_t dtc, bool testFailed);
  /// Status byte of @p dtc, or nullopt when unknown.
  std::optional<std::uint8_t> DtcStatus(std::uint32_t dtc);
  std::optional<DtcRecord> DtcInfo(std::uint32_t dtc);
  /// Attaches a snapshot record (freeze frame): the DIDs are encoded as 0x19 sub 0x04 expects them.
  void SetDtcSnapshot(std::uint32_t dtc, std::uint8_t recordNumber,
                      std::vector<std::pair<std::uint16_t, ByteVector>> const &dids);
  void SetDtcExtendedData(std::uint32_t dtc, std::uint8_t recordNumber, ByteVector data);
  /// Defines a groupOfDTC for 0x14 (e.g. 0x000100 = powertrain).
  void RegisterDtcGroup(std::uint32_t group, std::set<std::uint32_t> dtcs);
  /// Loads the event memory from @p storage and writes it back after every change.
  void SetDtcStorage(DtcStorage storage);

  /// Processes one request.  Returns nullopt when no response must be sent
  /// (suppressPositiveResponse).  Never throws.
  std::optional<ByteVector> Dispatch(ByteVector const &request, Addressing addressing = Addressing::kPhysical);

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
  std::optional<ByteVector> HandleSecurityAccess(ByteVector const &req);
  std::optional<ByteVector> HandleRoutineControl(ByteVector const &req);
  std::optional<ByteVector> HandleClearDtc(ByteVector const &req);
  std::optional<ByteVector> HandleReadDtc(ByteVector const &req);
  std::optional<ByteVector> HandleControlDtcSetting(ByteVector const &req);
  std::optional<ByteVector> HandleCommunicationControl(ByteVector const &req);
  std::optional<ByteVector> HandleReadMemory(ByteVector const &req);
  std::optional<ByteVector> HandleWriteMemory(ByteVector const &req);
  std::optional<ByteVector> HandleRequestTransfer(ByteVector const &req);
  std::optional<ByteVector> HandleTransferData(ByteVector const &req);
  std::optional<ByteVector> HandleTransferExit(ByteVector const &req);
  std::optional<ByteVector> DispatchPhysical(ByteVector const &request);
  void SaveDtcsLocked();
  void SetSessionLocked(uds::Session s);
  bool InSession(std::uint8_t mask) const { return (mask & uds::SessionBit(session_)) != 0; }

  Config cfg_;
  std::mutex mutex_; ///< guards registries and session state
  uds::Session session_{uds::Session::kDefault};
  TimePoint lastActivity_;
  std::map<std::uint16_t, DataIdentifier> dids_;
  std::map<std::uint8_t, Service> services_;
  ResetHandler reset_;
  std::map<std::uint16_t, Routine> routines_;
  SecurityProvider security_;
  std::uint8_t securityLevel_{0};
  std::uint8_t pendingLevel_{0};   ///< level of the last requestSeed, 0 = none
  ByteVector pendingSeed_;
  std::uint8_t failedAttempts_{0};
  TimePoint lockedUntil_{};
  DtcMemory dtcs_;
  std::map<std::uint32_t, std::set<std::uint32_t>> dtcGroups_;
  DtcStorage dtcStorage_;
  bool dtcSettingOn_{true};
  CommControlHandler commControl_;
  CommControlState commState_;
  ResponseSink pendingSink_;
  std::map<std::uint8_t, std::uint8_t> builtinSessions_; ///< SID -> allowed session mask
  std::vector<MemoryRegion> regions_;
  TransferHandler transfer_;
  struct TransferState {
    bool active{false};
    TransferDirection dir{TransferDirection::kDownload};
    std::uint64_t size{0};
    std::uint64_t done{0};
    std::uint32_t maxBlock{0};
    std::uint8_t nextCounter{1};
  } xfer_;
};

} // namespace ara::diag

#endif // ARA_DIAG_DIAGNOSTIC_SERVER_H_
