#include "dbmesh/core/logger.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace dbmesh {

const char* log_level_str(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::TRACE: return "TRACE";
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO ";
    case LogLevel::WARN:  return "WARN ";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::FATAL: return "FATAL";
  }
  return "UNKNOWN";
}

LogLevel parse_log_level(std::string_view s) {
  if (s == "TRACE") return LogLevel::TRACE;
  if (s == "DEBUG") return LogLevel::DEBUG;
  if (s == "INFO")  return LogLevel::INFO;
  if (s == "WARN")  return LogLevel::WARN;
  if (s == "ERROR") return LogLevel::ERROR;
  if (s == "FATAL") return LogLevel::FATAL;
  throw std::runtime_error(std::string("unknown log level: ") + std::string(s));
}

// ── Timestamp formatting ──────────────────────────────────────────────────

namespace {

std::string format_ts(std::chrono::system_clock::time_point tp) {
  auto tt = std::chrono::system_clock::to_time_t(tp);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()) % 1000;
  std::tm tm_buf{};
  ::gmtime_r(&tt, &tm_buf);
  char buf[32];
  snprintf(buf, sizeof(buf),
           "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
           tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
           tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
           static_cast<long>(ms.count()));
  return buf;
}

// ── Rotating file sink ────────────────────────────────────────────────────

class RotatingFileSink {
 public:
  RotatingFileSink(std::string path, uint32_t max_mb, uint32_t max_backups)
      : path_(std::move(path)),
        max_bytes_(static_cast<std::uintmax_t>(max_mb) * 1024ULL * 1024ULL),
        max_backups_(max_backups) {
    std::filesystem::create_directories(
        std::filesystem::path(path_).parent_path());
    stream_.open(path_, std::ios::app);
    if (!stream_) throw std::runtime_error("cannot open log file: " + path_);
  }

  void write(const std::string& line) {
    if (stream_.tellp() > 0 &&
        static_cast<std::uintmax_t>(stream_.tellp()) >= max_bytes_) {
      rotate();
    }
    stream_ << line << '\n';
    stream_.flush();
  }

 private:
  void rotate() {
    stream_.close();
    std::filesystem::remove(path_ + "." + std::to_string(max_backups_));
    for (int i = static_cast<int>(max_backups_) - 1; i >= 1; --i) {
      auto src = path_ + "." + std::to_string(i);
      auto dst = path_ + "." + std::to_string(i + 1);
      if (std::filesystem::exists(src)) std::filesystem::rename(src, dst);
    }
    if (std::filesystem::exists(path_))
      std::filesystem::rename(path_, path_ + ".1");
    stream_.open(path_, std::ios::app);
  }

  std::string       path_;
  std::uintmax_t    max_bytes_;
  uint32_t          max_backups_;
  std::ofstream     stream_;
};

// ── Log formatter functions ───────────────────────────────────────────────

std::string format_json(const LogEntry& e) {
  char buf[2048];
  auto ts = format_ts(e.timestamp);
  snprintf(buf, sizeof(buf),
           R"({"ts":"%s","level":"%s","component":"%s","msg":"%s"})",
           ts.c_str(),
           log_level_str(e.level),
           e.component.c_str(),
           e.message.c_str());
  return buf;
}

std::string format_text(const LogEntry& e) {
  char buf[2048];
  auto ts = format_ts(e.timestamp);
  snprintf(buf, sizeof(buf),
           "%s [%s] [%-10s] %s",
           ts.c_str(),
           log_level_str(e.level),
           e.component.c_str(),
           e.message.c_str());
  return buf;
}

// ── Global dispatcher ─────────────────────────────────────────────────────
// Single background writer thread; producers push under a mutex and
// signal the cv. The hot path only spends time under the lock to enqueue.

struct Dispatcher {
  std::mutex                                 mu;
  std::condition_variable                    cv;
  std::deque<LogEntry>                       queue;
  bool                                       shutdown_flag = false;
  std::thread                                writer;

  LogLevel                                   min_level  = LogLevel::INFO;
  bool                                       json_format = true;
  bool                                       to_stdout   = true;
  std::unique_ptr<RotatingFileSink>          file_sink;

  std::unordered_map<std::string, std::shared_ptr<Logger>> registry;
  std::mutex                                               registry_mu;

  void push(LogEntry entry) {
    if (static_cast<uint8_t>(entry.level) < static_cast<uint8_t>(min_level))
      return;
    {
      std::lock_guard lk(mu);
      queue.push_back(std::move(entry));
    }
    cv.notify_one();
  }

  void write_entry(const LogEntry& e) {
    std::string line = json_format ? format_json(e) : format_text(e);
    if (to_stdout) {
      if (e.level >= LogLevel::WARN)
        ::fputs((line + "\n").c_str(), stderr);
      else
        ::fputs((line + "\n").c_str(), stdout);
    }
    if (file_sink) file_sink->write(line);
  }

  void run() {
    for (;;) {
      std::deque<LogEntry> batch;
      {
        std::unique_lock lk(mu);
        cv.wait(lk, [this] { return !queue.empty() || shutdown_flag; });
        batch.swap(queue);
      }
      for (const auto& e : batch) write_entry(e);
      {
        std::lock_guard lk(mu);
        if (shutdown_flag && queue.empty()) return;
      }
    }
  }

  void start(const LoggingConfig& cfg) {
    min_level   = parse_log_level(cfg.level);
    json_format = (cfg.format == "json");
    to_stdout   = (cfg.output == "stdout" || cfg.output == "both");

    if (cfg.output == "file" || cfg.output == "both") {
      file_sink = std::make_unique<RotatingFileSink>(
          cfg.file.path, cfg.file.max_size_mb, cfg.file.max_backups);
    }

    writer = std::thread([this] { run(); });
  }

  void stop() {
    {
      std::lock_guard lk(mu);
      shutdown_flag = true;
    }
    cv.notify_one();
    if (writer.joinable()) writer.join();
  }

  static Dispatcher& instance() {
    static Dispatcher d;
    return d;
  }

 private:
  Dispatcher() = default;
};

} // namespace

// ── Logger ────────────────────────────────────────────────────────────────

Logger::Logger(std::string component) : component_(std::move(component)) {}

void Logger::log(LogLevel level, std::string_view msg) {
  Dispatcher::instance().push(LogEntry{
      level,
      component_,
      std::string(msg),
      std::chrono::system_clock::now(),
  });
}

void Logger::init(const LoggingConfig& config) {
  Dispatcher::instance().start(config);
}

std::shared_ptr<Logger> Logger::get(std::string_view component) {
  auto& d   = Dispatcher::instance();
  auto  key = std::string(component);
  {
    std::lock_guard lk(d.registry_mu);
    auto it = d.registry.find(key);
    if (it != d.registry.end()) return it->second;
    auto logger = std::make_shared<Logger>(key);
    d.registry.emplace(key, logger);
    return logger;
  }
}

void Logger::shutdown() {
  Dispatcher::instance().stop();
}

} // namespace dbmesh
