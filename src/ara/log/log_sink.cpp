#include "ara/log/log_sink.h"

#include <cstdio>
#include <ctime>

namespace ara::log {

std::string FormatRecord(LogRecord const &r) {
  auto const t = std::chrono::system_clock::to_time_t(r.timestamp);
  auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      r.timestamp.time_since_epoch())
                      .count() %
                  1000;
  std::tm tm{};
  gmtime_r(&t, &tm);
  char head[48];
  std::snprintf(head, sizeof head, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                static_cast<int>(ms));
  std::string out = head;
  out += ' ';
  out += r.appId;
  out += ' ';
  out += r.contextId;
  out += ' ';
  out += ToString(r.level);
  out += ' ';
  out += r.message;
  return out;
}

void ConsoleSink::Write(LogRecord const &r) {
  std::string const line = FormatRecord(r) + '\n';
  std::fwrite(line.data(), 1, line.size(), stderr);
}
void ConsoleSink::Flush() { std::fflush(stderr); }

std::unique_ptr<FileSink> FileSink::Open(std::string const &path) {
  std::FILE *f = std::fopen(path.c_str(), "a");
  if (f == nullptr) return nullptr;
  return std::unique_ptr<FileSink>(new FileSink(f));
}
FileSink::~FileSink() {
  if (file_ != nullptr) std::fclose(file_);
}
void FileSink::Write(LogRecord const &r) {
  std::string const line = FormatRecord(r) + '\n';
  std::fwrite(line.data(), 1, line.size(), file_);
}
void FileSink::Flush() { std::fflush(file_); }

} // namespace ara::log
