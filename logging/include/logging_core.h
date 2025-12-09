#pragma once

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

enum class Level { Info, Warn, Error };

struct LogEntry {
    std::string timestamp;
    Level level;
    std::string message;
};

class LoggerCore {
  public:
    void add(Level level, const std::string &msg);
    const std::vector<LogEntry> &entries() const { return m_entries; }

    static LoggerCore &instance(); // global singleton

  private:
    LoggerCore() = default;

    std::vector<LogEntry> m_entries;
    mutable std::mutex m_mutex;

    std::string getTimestamp() const;
};

// Convenience macros

#define LOG_INFO(fmt, ...) LoggerCore::instance().addFormatted(Level::Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LoggerCore::instance().addFormatted(Level::Warn, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LoggerCore::instance().addFormatted(Level::Error, fmt, ##__VA_ARGS__)
