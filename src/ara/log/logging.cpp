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

std::string FormatInteger(std::uint64_t magnitude, bool negative, unsigned bytes, Format f) {
  std::uint64_t const mask = bytes >= 8 ? ~std::uint64_t{0} : ((std::uint64_t{1} << (bytes * 8)) - 1);
  auto const pad = [&](std::string digits) {
    if (digits.size() < f.precision) digits.insert(0, f.precision - digits.size(), '0');
    return digits;
  };
  auto const radix = [&](unsigned shift, char const *alphabet) {
    std::uint64_t v = (negative ? (~magnitude + 1U) : magnitude) & mask;
    std::string d;
    do {
      d.insert(d.begin(), alphabet[v & ((1U << shift) - 1U)]);
      v >>= shift;
    } while (v != 0U);
    return pad(std::move(d));
  };
  switch (f.fmt) {
  case Fmt::kHex: return "0x" + radix(4, "0123456789ABCDEF");
  case Fmt::kBin: return "0b" + radix(1, "01");
  case Fmt::kOct: return "0o" + radix(3, "01234567");
  default: return (negative ? "-" : "") + pad(std::to_string(magnitude));
  }
}

std::string FormatFloating(double v, Format f) {
  char buf[512];
  int const prec = f.precision == 0 ? 6 : std::min(static_cast<int>(f.precision), 100);
  switch (f.fmt) {
  case Fmt::kEngFloat: std::snprintf(buf, sizeof buf, "%.*e", prec, v); break;
  case Fmt::kHexFloat:
    if (f.precision == 0) std::snprintf(buf, sizeof buf, "%a", v);
    else std::snprintf(buf, sizeof buf, "%.*a", prec, v);
    break;
  case Fmt::kAutoFloat: std::snprintf(buf, sizeof buf, "%.*g", prec, v); break;
  default: std::snprintf(buf, sizeof buf, "%.*f", prec, v); break;
  }
  return buf;
}

std::string FormatPointer(void const *p) {
  return "0x" + ToHex(reinterpret_cast<std::uintptr_t>(p), static_cast<unsigned>(sizeof(void *) * 2));
}

std::string FormatErrorCode(ara::core::ErrorCode const &ec) {
  return std::string(ec.Domain().Name()) + ":" + std::to_string(ec.Value()) + " (" + ec.Message() + ")";
}

void Emit(LogLevel level, std::string const &ctx, std::string message, std::string tag,
          std::string file, int line) {
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
  rec.tag = std::move(tag);
  rec.file = std::move(file);
  rec.line = line;
  rec.timestamp = std::chrono::system_clock::now();
  for (auto &sink : sinks) sink->Write(rec);
}
} // namespace detail

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

Logger &CreateLogger(std::string_view ctxId, std::string_view ctxDescription) {
  return CreateLogger(ctxId, ctxDescription, LogLevel::kWarn);
}

Logger &CreateLogger(ara::core::InstanceSpecifier const &instanceSpec) {
  std::string_view id = instanceSpec.ToString();
  auto const slash = id.rfind('/');
  if (slash != std::string_view::npos) id.remove_prefix(slash + 1);
  return CreateLogger(id, {}, LogLevel::kWarn);
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
    : logger_(o.logger_), level_(o.level_), enabled_(o.enabled_), buffer_(std::move(o.buffer_)),
      tag_(std::move(o.tag_)), file_(std::move(o.file_)), line_(o.line_) {
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
    tag_ = std::move(o.tag_);
    file_ = std::move(o.file_);
    line_ = o.line_;
    o.enabled_ = false;
    o.buffer_.clear();
  }
  return *this;
}
LogStream::~LogStream() { Flush(); }

void LogStream::Flush() noexcept {
  if (!enabled_ || logger_ == nullptr) return;
  try {
    detail::Emit(level_, logger_->ContextId(), std::move(buffer_), std::move(tag_),
                 std::move(file_), line_);
  } catch (...) { // logging must never throw
  }
  buffer_.clear();
  tag_.clear();
  file_.clear();
  line_ = 0;
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
LogStream &LogStream::WithLocation(std::string_view file, int line) noexcept {
  if (enabled_) {
    try {
      file_.assign(file);
      line_ = line;
    } catch (...) {
    }
  }
  return *this;
}
LogStream &LogStream::WithTag(std::string_view tag) noexcept {
  if (enabled_) {
    try {
      tag_.assign(tag);
    } catch (...) {
    }
  }
  return *this;
}
LogStream &LogStream::operator<<(void const *p) { return enabled_ ? Append(detail::FormatPointer(p)) : *this; }
LogStream &LogStream::operator<<(ara::core::ErrorCode const &ec) {
  return enabled_ ? Append(detail::FormatErrorCode(ec)) : *this;
}
LogStream &LogStream::operator<<(ara::core::InstanceSpecifier const &is) { return Append(is.ToString()); }

LogStream &LogStream::operator<<(ara::core::Span<const ara::core::Byte> bytes) {
  if (!enabled_) return *this;
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 3);
  for (auto const b : bytes) {
    auto const v = static_cast<unsigned>(b);
    if (!out.empty()) out.push_back(' ');
    out.push_back(kDigits[v >> 4]);
    out.push_back(kDigits[v & 0xF]);
  }
  return Append(out);
}
LogStream &LogStream::operator<<(float v) { return enabled_ ? Append(std::to_string(v)) : *this; }
LogStream &LogStream::operator<<(double v) { return enabled_ ? Append(std::to_string(v)) : *this; }

} // namespace ara::log
