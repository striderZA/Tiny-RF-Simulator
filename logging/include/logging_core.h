#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <ctime>

enum class Level {
	Info,
	Warn,
	Error
};

struct LogEntry {
	std::string timestamp;
	Level level;
	std::string message;
};

class LoggerCore {
public:
	void add(Level level, const std::string& msg);
	const std::vector<LogEntry>& entries() const { return m_entries; }

	static LoggerCore& instance();   // global singleton

private:
	LoggerCore() = default;

	std::vector<LogEntry> m_entries;
	mutable std::mutex m_mutex;

	std::string getTimestamp() const;
};

// Convenience macros
#define LOG_INFO(fmt, ...)  simulator::log::LoggerCore::instance().add(simulator::log::Level::Info,  fmt)
#define LOG_WARN(fmt, ...)  simulator::log::LoggerCore::instance().add(simulator::log::Level::Warn,  fmt)
#define LOG_ERROR(fmt, ...) simulator::log::LoggerCore::instance().add(simulator::log::Level::Error, fmt)
