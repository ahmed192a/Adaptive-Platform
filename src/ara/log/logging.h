/**
 * @file logging.h
 * @brief ara::log public API: InitLogging, CreateLogger, sinks (SWS_LOG).
 *
 * Environment overrides read by InitLogging():
 *   ARA_LOG_LEVEL = off|fatal|error|warn|info|debug|verbose
 *   ARA_LOG_MODE  = comma list of console,file,remote
 *   ARA_LOG_FILE  = path used by the file sink
 */
#ifndef ARA_LOG_LOGGING_H_
#define ARA_LOG_LOGGING_H_

#include <memory>
#include <string>
#include <string_view>

#include "ara/log/log_sink.h"
#include "ara/log/log_stream.h"
#include "ara/log/logger.h"
#include "ara/log/logging_types.h"

namespace ara::log {

/// SWS_LOG_00001. Idempotent; the first call wins.  Console is the default sink.
void InitLogging(std::string_view appId, std::string_view appDescription, LogLevel appDefLogLevel,
                 LogMode logMode, std::string_view directoryPath = {});

/// SWS_LOG_00002. Returns the logger of the context, creating it on first use.
/// The returned reference stays valid until ara::core::Deinitialize().
Logger &CreateLogger(std::string_view ctxId, std::string_view ctxDescription,
                     LogLevel ctxDefLogLevel = LogLevel::kWarn);

/// State of the remote (DLT-like) client; always kNotConnected until a remote sink exists.
ClientState GetClientState() noexcept;

namespace apext {
/// Adds a sink (e.g. a capturing sink in tests).  Not part of the AUTOSAR API.
void AddSink(std::shared_ptr<LogSink> sink);
void ClearSinks();
std::string const &ApplicationId();
} // namespace apext

} // namespace ara::log

#endif // ARA_LOG_LOGGING_H_
