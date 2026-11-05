/**
 * @file log_sink.h
 * @brief Pluggable output of the logging framework (implementation extension).
 *
 * The AUTOSAR spec only defines the API; the transport (DLT, console, file) is
 * implementation-defined.  Sinks let tests and integrators capture records.
 */
#ifndef ARA_LOG_LOG_SINK_H_
#define ARA_LOG_LOG_SINK_H_

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "ara/log/logging_types.h"

namespace ara::log {

struct LogRecord {
  LogLevel level{LogLevel::kInfo};
  std::string appId;
  std::string contextId;
  std::string message;
  std::chrono::system_clock::time_point timestamp;
};

class LogSink {
public:
  virtual ~LogSink() = default;
  virtual void Write(LogRecord const &record) = 0;
  virtual void Flush() {}
};

/// Formats records as "<time> <APID> <CTID> <LVL> message" on stderr.
class ConsoleSink final : public LogSink {
public:
  void Write(LogRecord const &record) override;
  void Flush() override;
};

/// Appends formatted records to a file.
class FileSink final : public LogSink {
public:
  /// Returns nullptr when the file cannot be opened.
  static std::unique_ptr<FileSink> Open(std::string const &path);
  ~FileSink() override;
  void Write(LogRecord const &record) override;
  void Flush() override;

private:
  explicit FileSink(std::FILE *f) : file_(f) {}
  std::FILE *file_;
};

std::string FormatRecord(LogRecord const &record);

} // namespace ara::log

#endif // ARA_LOG_LOG_SINK_H_
