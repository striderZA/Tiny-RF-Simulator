#include "agent_wire.h"

AgentLineReader::AgentLineReader(std::size_t max_line) : m_max_line(max_line) {}

void AgentLineReader::append(std::string_view bytes) { m_buffer.append(bytes); }

AgentLineReader::Next AgentLineReader::next(std::string &line) {
    for (;;) {
        if (m_discarding_oversized) {
            const std::size_t newline = m_buffer.find('\n');
            if (newline == std::string::npos) {
                m_buffer.clear();
                return Next::NeedMore;
            }
            m_buffer.erase(0, newline + 1);
            m_discarding_oversized = false;
        }

        const std::size_t newline = m_buffer.find('\n');
        if (newline != std::string::npos) {
            std::size_t line_size = newline;
            if (line_size > 0 && m_buffer[line_size - 1] == '\r') {
                --line_size;
            }
            if (line_size > m_max_line) {
                m_buffer.erase(0, newline + 1);
                return Next::Oversized;
            }
            line.assign(m_buffer.data(), line_size);
            m_buffer.erase(0, newline + 1);
            return Next::Line;
        }

        std::size_t partial_size = m_buffer.size();
        if (partial_size > 0 && m_buffer.back() == '\r') {
            --partial_size;
        }
        if (partial_size > m_max_line) {
            m_buffer.clear();
            m_discarding_oversized = true;
            return Next::Oversized;
        }
        return Next::NeedMore;
    }
}

std::string agentLine(const nlohmann::json &message) {
    std::string line = message.dump();
    line.push_back('\n');
    return line;
}
