#include "ara/log/logging.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

#include "ara/core/initialization.h"

namespace ara::log {
namespace {

struct State {
  std::mutex mutex;
  bool initialized{false};
  std::string appId{"APP"};
  std::string appDescription;
  LogLevel defaultLevel{LogLevel::kWarn};
  std::vector<std::shared_ptr<LogSink>> sinks;
  std::map<std::string, std::unique_ptr<Logger>, std::less<>> loggers;
};

State &S() {
  static State s;
  return s;
}

bool ParseLevel(std::string v, LogLevel &out) {
  std::transform(v.begin(), v.end(), v.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  static constexpr struct { char const *n; LogLevel l; } kMap[] = {
      {"off", LogLevel::kOff},     {"fatal", LogLevel::kFatal}, {"error", LogLevel::kError},
      {"warn", LogLevel::kWarn},   {"info", LogLevel::kInfo},   {"debug", LogLevel::kDebug},
      {"verbose", LogLevel::kVerbose}};
  for (auto const &e : kMap)
    if (v == e.n) { out = e.l; return true; }
  return false;
}

LogMode ModeFromEnv(LogMode fallback) {
  char const *m = std::getenv("ARA_LOG_MODE");
  if (m == nullptr) return fallback;
  std::string v = m;
  std::uint8_t bits = 0;
  if (v.find("console") != std::string::npos) bits |= static_cast<std::uint8_t>(LogMode::kConsole);
  if (v.find("file") != std::string::npos) bits |= static_cast<std::uint8_t>(LogMode::kFile);
  if (v.find("remote") != std::string::npos) bits |= static_cast<std::uint8_t>(LogMode::kRemote);
  return bits == 0 ? fallback : static_cast<LogMode>(bits);
}

void ResetState() {
  auto &s = S();
  std::lock_guard<std::mutex> lk(s.mutex);
  for (auto &sink : s.sinks) sink->Flush();
  s.sinks.clear();
  s.loggers.clear();
  s.initialized = false;
  s.appId = "APP";
  s.defaultLevel = LogLevel::kWarn;
}

ara::core::Result<void> InitLog() { return {}; }
ara::core::Result<void> DeinitLog() {
  ResetState();
  return {};
}
ARA_REGISTER_LIFECYCLE_HOOK("ara::log", &InitLog, &DeinitLog)

} // namespace

namespace detail {
std::string ToHex(std::uint64_t v, unsigned digits) {
  static char const *d = "0123456789ABCDEF";
  std::string s(digits, '0');
  for (unsigned i = digits; i > 0; --i, v >>= 4) s[i - 1] = d[v & 0xF];
  return s;
}
std::string ToBin(std::uint64_t v, unsigned bits) {
  std::string s(bits, '0');
  for (unsigned i = bits; i > 0; --i, v >>= 1) s[i - 1] = static_cast<char>('0' + (v & 1));
  return s;
}

void Emit(LogLevel level, std::string const &ctx, std::string message) {
  auto &s = S();
  std::vector<std::shared_ptr<LogSink>> sinks;
  LogRecord rec;
  {
    std::lock_guard<std::mutex> lk(s.mutex);
    sinks = s.sinks;
    rec.appId = s.appId;
  }
  rec.level = level;
  rec.contextId = ctx;
  rec.message = std::move(message);
  rec.timestamp = std::chrono::system_clock::now();
  for (auto &sink : sinks) sink->Write(rec);
}
} // namespace detail

namespace detail { void Emit(LogLevel, std::string const &, std::string); }
void InitLogging(std::string_view appId, std::string_view appDescription, LogLevel appDefLogLevel,
                 LogMode logMode, std::string_view directoryPath) {
  auto &s = S();
  std::lock_guard<std::mutex> lk(s.mutex);
  if (s.initialized) return;
  s.initialized = true;
  s.appId.assign(appId.substr(0, 4));
  s.appDescription.assign(appDescription);
  s.defaultLevel = appDefLogLevel;
  if (char const *lv = std::getenv("ARA_LOG_LEVEL")) ParseLevel(lv, s.defaultLevel);
  LogMode const mode = ModeFromEnv(logMode);
  if (HasMode(mode, LogMode::kConsole)) s.sinks.push_back(std::make_shared<ConsoleSink>());
  if (HasMode(mode, LogMode::kFile)) {
    std::string path;
    if (char const *f = std::getenv("ARA_LOG_FILE")) path = f;
    else {
      path.assign(directoryPath);
      if (!path.empty() && path.back() != '/') path += '/';
      path += s.appId + ".log";
    }
    if (auto fs = FileSink::Open(path)) s.sinks.push_back(std::move(fs));
  }
  // kRemote: no transport in this implementation; records are not forwarded.
}

Logger &CreateLogger(std::string_view ctxId, std::string_view ctxDescription,
                     LogLevel ctxDefLogLevel) {
  auto &s = S();
  std::lock_guard<std::mutex> lk(s.mutex);
  std::string key(ctxId.substr(0, 4));
  auto it = s.loggers.find(key);
  if (it != s.loggers.end()) return *it->second;
  if (!s.initialized) {
    // Implicit default configuration: console sink, level from the first logger.
    s.initialized = true;
    s.defaultLevel = ctxDefLogLevel;
    if (char const *lv = std::getenv("ARA_LOG_LEVEL")) ParseLevel(lv, s.defaultLevel);
    s.sinks.push_back(std::make_shared<ConsoleSink>());
  }
  LogLevel lvl = ctxDefLogLevel;
  if (char const *lv = std::getenv("ARA_LOG_LEVEL")) ParseLevel(lv, lvl);
  auto *raw = new Logger(key, std::string(ctxDescription), lvl);
  s.loggers.emplace(key, std::unique_ptr<Logger>(raw));
  return *raw;
}

ClientState GetClientState() noexcept { return ClientState::kNotConnected; }

namespace apext {
void AddSink(std::shared_ptr<LogSink> sink) {
  auto &s = S();
  std::lock_guard<std::mutex> lk(s.mutex);
  s.sinks.push_back(std::move(sink));
}
void ClearSinks() {
  auto &s = S();
  std::lock_guard<std::mutex> lk(s.mutex);
  s.sinks.clear();
  s.initialized = true; // explicit sink configuration: no implicit console sink
}
std::string const &ApplicationId() { return S().appId; }
} // namespace apext

// ------------------------------------------------------------------ Logger
bool Logger::IsEnabled(LogLevel level) const noexcept {
  auto const cur = level_.load();
  return level != LogLevel::kOff && cur != LogLevel::kOff &&
         static_cast<std::uint8_t>(level) <= static_cast<std::uint8_t>(cur);
}
LogStream Logger::WithLevel(LogLevel level) const noexcept {
  return LogStream(this, level, IsEnabled(level));
}

// --------------------------------------------------------------- LogStream
LogStream::LogStream(Logger const *l, LogLevel level, bool enabled) noexcept
    : logger_(l), level_(level), enabled_(enabled) {}
LogStream::LogStream(LogStream &&o) noexcept
    : logger_(o.logger_), level_(o.level_), enabled_(o.enabled_), buffer_(std::move(o.buffer_)) {
  o.enabled_ = false;
  o.buffer_.clear();
}
LogStream &LogStream::operator=(LogStream &&o) noexcept {
  if (this != &o) {
    Flush();
    logger_ = o.logger_;
    level_ = o.level_;
    enabled_ = o.enabled_;
    buffer_ = std::move(o.buffer_);
    o.enabled_ = false;
    o.buffer_.clear();
  }
  return *this;
}
LogStream::~LogStream() { Flush(); }

void LogStream::Flush() noexcept {
  if (!enabled_ || logger_ == nullptr) return;
  try {
    detail::Emit(level_, logger_->ContextId(), std::move(buffer_));
  } catch (...) { // logging must never throw
  }
  buffer_.clear();
}

LogStream &LogStream::Append(std::string_view s) {
  if (enabled_) buffer_.append(s);
  return *this;
}
LogStream &LogStream::operator<<(bool v) { return Append(v ? "true" : "false"); }
LogStream &LogStream::operator<<(char v) { return Append(std::string_view(&v, 1)); }
LogStream &LogStream::operator<<(char const *v) { return Append(v == nullptr ? "(null)" : v); }
LogStream &LogStream::operator<<(std::string const &v) { return Append(v); }
LogStream &LogStream::operator<<(std::string_view v) { return Append(v); }
LogStream &LogStream::operator<<(float v) { return enabled_ ? Append(std::to_string(v)) : *this; }
LogStream &LogStream::operator<<(double v) { return enabled_ ? Append(std::to_string(v)) : *this; }

} // namespace ara::log
