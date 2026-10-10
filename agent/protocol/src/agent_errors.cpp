#include "agent_errors.h"

#include <cmath>

std::string_view agentErrorCodeName(AgentErrorCode code) {
    switch (code) {
    case AgentErrorCode::InvalidArgument:
        return "INVALID_ARGUMENT";
    case AgentErrorCode::NotFound:
        return "NOT_FOUND";
    case AgentErrorCode::StaleEpoch:
        return "STALE_EPOCH";
    case AgentErrorCode::UnknownType:
        return "UNKNOWN_TYPE";
    case AgentErrorCode::UnknownPart:
        return "UNKNOWN_PART";
    case AgentErrorCode::AmbiguousPart:
        return "AMBIGUOUS_PART";
    case AgentErrorCode::ParamRejected:
        return "PARAM_REJECTED";
    case AgentErrorCode::PathParamsUnsupported:
        return "PATH_PARAMS_UNSUPPORTED";
    case AgentErrorCode::LinkRejected:
        return "LINK_REJECTED";
    case AgentErrorCode::NoMeasurement:
        return "NO_MEASUREMENT";
    case AgentErrorCode::Busy:
        return "BUSY";
    case AgentErrorCode::SimulatorUnavailable:
        return "SIMULATOR_UNAVAILABLE";
    case AgentErrorCode::VersionMismatch:
        return "VERSION_MISMATCH";
    case AgentErrorCode::Internal:
        return "INTERNAL";
    }
    return "INTERNAL";
}

nlohmann::json agentErrorJson(const AgentError &error) {
    nlohmann::json result = {{"code", std::string(agentErrorCodeName(error.code))},
                             {"message", error.message}};
    if (error.hint.has_value()) {
        result["hint"] = *error.hint;
    }
    if (error.op_index.has_value()) {
        result["op_index"] = *error.op_index;
    }
    if (!error.details.is_null() && !error.details.empty()) {
        result["details"] = error.details;
    }
    return result;
}

AgentToolResult agentErrorResult(const AgentError &error, std::optional<std::uint64_t> epoch) {
    AgentToolResult result;
    result.is_error = true;
    result.structured = {
        {"epoch", epoch.has_value() ? nlohmann::json(*epoch) : nlohmann::json(nullptr)},
        {"error", agentErrorJson(error)}};
    return result;
}

nlohmann::json agentNumber(double value) {
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}
