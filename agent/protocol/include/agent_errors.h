#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class AgentErrorCode {
    InvalidArgument,
    NotFound,
    StaleEpoch,
    UnknownType,
    UnknownPart,
    AmbiguousPart,
    ParamRejected,
    PathParamsUnsupported,
    LinkRejected,
    NoMeasurement,
    Busy,
    SimulatorUnavailable,
    VersionMismatch,
    Internal,
};

std::string_view agentErrorCodeName(AgentErrorCode code);

struct AgentError {
    AgentErrorCode code;
    std::string message;
    std::optional<std::string> hint;
    std::optional<int> op_index;
    nlohmann::json details = nlohmann::json::object();
};

struct AgentToolResult {
    bool is_error = false;
    nlohmann::json structured;
};

nlohmann::json agentErrorJson(const AgentError &error);
AgentToolResult agentErrorResult(const AgentError &error, std::optional<std::uint64_t> epoch);
nlohmann::json agentNumber(double value);
