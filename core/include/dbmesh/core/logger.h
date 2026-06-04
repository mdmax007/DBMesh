#ifndef DBMESH_CORE_LOGGER_H_
#define DBMESH_CORE_LOGGER_H_

#include "dbmesh/core/config.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace dbmesh {

enum class LogLevel : uint8_t {
  TRACE = 0,
  DEBUG = 1,
  INFO  = 2,
  WARN  = 3,
  ERROR = 4,
  FATAL = 5,
};

[[nodiscard]] const char* log_level_str(LogLevel level) noexcept;
// Not nodiscard — callers may invoke solely to validate a string (will throw).
LogLevel parse_log_level(std::string_view s);

// ── Log entry ─────────────────────────────────────────────────────────────

struct LogEntry {
  LogLevel                                    level;
  std::string                                 component;
  std::string                                 message;
  std::chrono::system_clock::time_point       timestamp;
};

// ── Logger ────────────────────────────────────────────────────────────────
// Obtained via Logger::get("component"). All loggers share one background
// writer thread; log() never blocks the calling (hot-path) thread.

class Logger {
 public:
  explicit Logger(std::string component);

  void log(LogLevel level, std::string_view msg);

  void trace(std::string_view msg) { log(LogLevel::TRACE, msg); }
  void debug(std::string_view msg) { log(LogLevel::DEBUG, msg); }
  void info(std::string_view msg)  { log(LogLevel::INFO,  msg); }
  void warn(std::string_view msg)  { log(LogLevel::WARN,  msg); }
  void error(std::string_view msg) { log(LogLevel::ERROR, msg); }
  void fatal(std::string_view msg) { log(LogLevel::FATAL, msg); }

  [[nodiscard]] const std::string& component() const noexcept { return component_; }

  // ── Global registry ─────────────────────────────────────────────────────
  // init() must be called once at startup before any Logger::get() calls.
  static void                    init(const LoggingConfig& config);
  static std::shared_ptr<Logger> get(std::string_view component);
  // Flushes remaining entries and stops the writer thread.
  static void                    shutdown();

 private:
  std::string component_;
};

} // namespace dbmesh

#endif // DBMESH_CORE_LOGGER_H_
