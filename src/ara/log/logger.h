/**
 * @file logger.h
 * @brief ara::log::Logger (SWS_LOG_00018...) - one logger per logging context.
 */
#ifndef ARA_LOG_LOGGER_H_
#define ARA_LOG_LOGGER_H_

#include <atomic>
#include <string>

#include "ara/core/instance_specifier.h"
#include "ara/log/log_stream.h"
#include "ara/log/logging_types.h"

namespace ara::log {

class Logger final {
public:
  Logger(Logger const &) = delete;
  Logger &operator=(Logger const &) = delete;

  LogStream LogFatal() const noexcept { return WithLevel(LogLevel::kFatal); }
  LogStream LogError() const noexcept { return WithLevel(LogLevel::kError); }
  LogStream LogWarn() const noexcept { return WithLevel(LogLevel::kWarn); }
  LogStream LogInfo() const noexcept { return WithLevel(LogLevel::kInfo); }
  LogStream LogDebug() const noexcept { return WithLevel(LogLevel::kDebug); }
  LogStream LogVerbose() const noexcept { return WithLevel(LogLevel::kVerbose); }

  LogStream WithLevel(LogLevel level) const noexcept;
  bool IsEnabled(LogLevel level) const noexcept;

  std::string const &ContextId() const noexcept { return contextId_; }
  std::string const &ContextDescription() const noexcept { return description_; }
  LogLevel GetLevel() const noexcept { return level_.load(); }
  /// SWS_LOG_00254: sets the severity threshold of this logger.
  void SetThreshold(LogLevel threshold) noexcept { level_.store(threshold); }
  /// Deprecated alias kept for older callers of this repository.
  void SetLevel(LogLevel l) noexcept { SetThreshold(l); }

private:
  friend Logger &CreateLogger(std::string_view, std::string_view, LogLevel);
  friend Logger &CreateLogger(ara::core::InstanceSpecifier const &);
  Logger(std::string ctx, std::string desc, LogLevel level)
      : contextId_(std::move(ctx)), description_(std::move(desc)), level_(level) {}

  std::string contextId_;
  std::string description_;
  std::atomic<LogLevel> level_;
};

} // namespace ara::log

#endif // ARA_LOG_LOGGER_H_
