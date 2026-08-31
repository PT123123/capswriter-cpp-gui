#pragma once

// Minimal logging utility for CapsWriter C++ client.

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>

namespace capswriter {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

class Logger {
 public:
  static Logger& Instance() {
    static Logger logger;
    return logger;
  }

  void set_level(LogLevel level) { level_ = level; }
  void set_level(const std::string& name) {
    if (name == "DEBUG") level_ = LogLevel::kDebug;
    else if (name == "INFO") level_ = LogLevel::kInfo;
    else if (name == "WARNING") level_ = LogLevel::kWarn;
    else if (name == "ERROR") level_ = LogLevel::kError;
  }

  // Optional sink receiving fully formatted lines ("2026-08-31 12:00:00
  // INFO message"). Called from arbitrary threads while the logger mutex
  // is held, so implementations must be cheap and must not log.
  void set_sink(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
  }

  void Log(LogLevel level, const char* fmt, ...) {
    if (level < level_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char* tag = "INFO";
    if (level == LogLevel::kDebug) tag = "DEBUG";
    if (level == LogLevel::kWarn) tag = "WARN";
    if (level == LogLevel::kError) tag = "ERROR";

    va_list args;
    va_start(args, fmt);
    char buf[2048];
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) n = 0;

    const std::string line =
        std::string(ts) + " " + tag + " " + std::string(buf, n);

    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);

    if (sink_) sink_(line);
  }

 private:
  std::mutex mutex_;
  LogLevel level_ = LogLevel::kInfo;
  std::function<void(const std::string&)> sink_;
};

#define LOG_DEBUG(...) ::capswriter::Logger::Instance().Log(::capswriter::LogLevel::kDebug, __VA_ARGS__)

#define LOG_INFO(...) ::capswriter::Logger::Instance().Log(::capswriter::LogLevel::kInfo, __VA_ARGS__)
#define LOG_WARN(...) ::capswriter::Logger::Instance().Log(::capswriter::LogLevel::kWarn, __VA_ARGS__)
#define LOG_ERROR(...) ::capswriter::Logger::Instance().Log(::capswriter::LogLevel::kError, __VA_ARGS__)

}  // namespace capswriter
