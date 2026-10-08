#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>

inline constexpr std::string_view kAgentWireProtocol = "rfsim-agent/1";
inline constexpr std::size_t kAgentMaxLineBytes = std::size_t{1} << 20;
inline constexpr int kAgentRpcErrorCode = -32000;

class AgentLineReader {
  public:
    enum class Next { Line, NeedMore, Oversized };

    explicit AgentLineReader(std::size_t max_line = kAgentMaxLineBytes);
    void append(std::string_view bytes);
    Next next(std::string &line);

  private:
    std::size_t m_max_line;
    std::string m_buffer;
    bool m_discarding_oversized{false};
};

std::string agentLine(const nlohmann::json &message);
