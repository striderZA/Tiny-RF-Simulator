#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

inline constexpr std::string_view kAgentWireProtocol = "rfsim-agent/1";
inline constexpr std::size_t kAgentMaxLineBytes = std::size_t{1} << 20;
inline constexpr int kAgentRpcErrorCode = -32000;
// Maximum container nesting in an agent JSON message. Tool calls nest a few levels; the limit
// keeps recursive copies and dumps of hostile input from exhausting the stack.
inline constexpr std::size_t kAgentMaxJsonDepth = 64;

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

// True when no array or object in `value` nests deeper than `max_depth` containers. The walk uses
// an explicit stack, so the check itself cannot overflow on the input it rejects.
template <typename Json>
bool agentJsonDepthWithin(const Json &value, std::size_t max_depth = kAgentMaxJsonDepth) {
    std::vector<std::pair<const Json *, std::size_t>> pending;
    pending.emplace_back(&value, std::size_t{0});
    while (!pending.empty()) {
        const auto [node, enclosing] = pending.back();
        pending.pop_back();
        if (!node->is_array() && !node->is_object())
            continue;
        if (enclosing + 1 > max_depth)
            return false;
        for (const auto &child : *node)
            pending.emplace_back(&child, enclosing + 1);
    }
    return true;
}
