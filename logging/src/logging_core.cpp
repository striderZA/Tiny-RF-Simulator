#include "logging_core.h"
#include <cstdarg>
#include <cstdio>

LoggerCore& LoggerCore::instance() {
	static LoggerCore inst;
	return inst;
}

std::string LoggerCore::getTimestamp() const {
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
	return buffer;
}

void LoggerCore::add(Level level, const std::string& msg) {
	std::lock_guard<std::mutex> lock(m_mutex);

	m_entries.push_back({ getTimestamp(), level, msg });
}