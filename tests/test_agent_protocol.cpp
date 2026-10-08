#if __has_include("imgui.h") || __has_include("circuit_runtime.h") ||                              \
                                              __has_include("editor_commands.h")
#error "agent_protocol must stay independent of the simulator and the UI"
#endif

#include "agent_errors.h"
#include "agent_wire.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

TEST_CASE("Line reader splits complete and partial lines and strips one CR", "[agent_protocol]") {
    CHECK(kAgentWireProtocol == "rfsim-agent/1");
    CHECK(kAgentMaxLineBytes == (std::size_t{1} << 20));
    AgentLineReader reader;
    std::string line;

    reader.append("a\r\nb");
    REQUIRE(reader.next(line) == AgentLineReader::Next::Line);
    CHECK(line == "a");
    CHECK(reader.next(line) == AgentLineReader::Next::NeedMore);

    reader.append("\n");
    REQUIRE(reader.next(line) == AgentLineReader::Next::Line);
    CHECK(line == "b");
    CHECK(reader.next(line) == AgentLineReader::Next::NeedMore);

    reader.append("hel");
    CHECK(reader.next(line) == AgentLineReader::Next::NeedMore);
    reader.append("lo\n");
    REQUIRE(reader.next(line) == AgentLineReader::Next::Line);
    CHECK(line == "hello");
}

TEST_CASE("An oversized line is reported once before the reader resynchronizes",
          "[agent_protocol]") {
    AgentLineReader reader{4};
    std::string line;

    reader.append("12345");
    CHECK(reader.next(line) == AgentLineReader::Next::Oversized);
    CHECK(reader.next(line) == AgentLineReader::Next::NeedMore);

    reader.append("6\nok\n");
    REQUIRE(reader.next(line) == AgentLineReader::Next::Line);
    CHECK(line == "ok");
    CHECK(reader.next(line) == AgentLineReader::Next::NeedMore);
}

TEST_CASE("Every agent error code maps to its protocol name", "[agent_protocol]") {
    constexpr std::array cases{
        std::pair{AgentErrorCode::InvalidArgument, std::string_view{"INVALID_ARGUMENT"}},
        std::pair{AgentErrorCode::NotFound, std::string_view{"NOT_FOUND"}},
        std::pair{AgentErrorCode::StaleEpoch, std::string_view{"STALE_EPOCH"}},
        std::pair{AgentErrorCode::UnknownType, std::string_view{"UNKNOWN_TYPE"}},
        std::pair{AgentErrorCode::UnknownPart, std::string_view{"UNKNOWN_PART"}},
        std::pair{AgentErrorCode::AmbiguousPart, std::string_view{"AMBIGUOUS_PART"}},
        std::pair{AgentErrorCode::ParamRejected, std::string_view{"PARAM_REJECTED"}},
        std::pair{AgentErrorCode::PathParamsUnsupported,
                  std::string_view{"PATH_PARAMS_UNSUPPORTED"}},
        std::pair{AgentErrorCode::LinkRejected, std::string_view{"LINK_REJECTED"}},
        std::pair{AgentErrorCode::NoMeasurement, std::string_view{"NO_MEASUREMENT"}},
        std::pair{AgentErrorCode::Busy, std::string_view{"BUSY"}},
        std::pair{AgentErrorCode::SimulatorUnavailable, std::string_view{"SIMULATOR_UNAVAILABLE"}},
        std::pair{AgentErrorCode::VersionMismatch, std::string_view{"VERSION_MISMATCH"}},
        std::pair{AgentErrorCode::Internal, std::string_view{"INTERNAL"}},
    };

    for (const auto &[code, expected] : cases) {
        CHECK(agentErrorCodeName(code) == expected);
    }
    CHECK(kAgentRpcErrorCode == -32000);
}

TEST_CASE("Agent error results use the common shape and omit unset optional fields",
          "[agent_protocol]") {
    AgentError error{AgentErrorCode::InvalidArgument, "invalid input"};

    CHECK((agentErrorJson(error) ==
           nlohmann::json{{"code", "INVALID_ARGUMENT"}, {"message", "invalid input"}}));

    const AgentToolResult with_epoch = agentErrorResult(error, 3);
    REQUIRE(with_epoch.is_error);
    REQUIRE(with_epoch.structured.is_object());
    CHECK(with_epoch.structured.size() == 2);
    CHECK(with_epoch.structured.at("epoch") == 3);
    CHECK(with_epoch.structured.at("error") == agentErrorJson(error));
    CHECK_FALSE(with_epoch.structured.at("error").contains("hint"));
    CHECK_FALSE(with_epoch.structured.at("error").contains("op_index"));
    CHECK_FALSE(with_epoch.structured.at("error").contains("details"));

    error.hint = "check the argument path";
    error.op_index = 2;
    error.details = {{"path", "params.gain_dB"}};
    const AgentToolResult without_epoch = agentErrorResult(error, std::nullopt);
    REQUIRE(without_epoch.is_error);
    CHECK(without_epoch.structured.at("epoch").is_null());
    CHECK(without_epoch.structured.at("error").at("code") == "INVALID_ARGUMENT");
    CHECK(without_epoch.structured.at("error").at("message") == "invalid input");
    CHECK(without_epoch.structured.at("error").at("hint") == "check the argument path");
    CHECK(without_epoch.structured.at("error").at("op_index") == 2);
    CHECK(without_epoch.structured.at("error").at("details").at("path") == "params.gain_dB");
}

TEST_CASE("Agent wire lines contain one serialized JSON message and a newline",
          "[agent_protocol]") {
    const std::string line = agentLine(nlohmann::json{{"method", "hello"}, {"id", 7}});
    CHECK(line == "{\"id\":7,\"method\":\"hello\"}\n");
}

TEST_CASE("Non-finite numbers encode as null and finite numbers stay numeric", "[agent_protocol]") {
    CHECK(agentNumber(12.5) == nlohmann::json(12.5));
    CHECK(agentNumber(std::numeric_limits<double>::quiet_NaN()).is_null());
    CHECK(agentNumber(std::numeric_limits<double>::infinity()).is_null());
    CHECK(agentNumber(-std::numeric_limits<double>::infinity()).is_null());
}
