#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#if __has_include("imgui.h") || __has_include("circuit_runtime.h") ||                              \
                                              __has_include("editor_commands.h")
#error "agent_protocol must stay independent of the simulator and the UI"
#endif

#include "agent_endpoint.h"
#include "agent_errors.h"
#include "agent_token.h"
#include "agent_wire.h"
#include "test_temp_paths.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("Generated agent tokens are 64 lowercase hex characters and differ", "[agent_protocol]") {
    const auto first = generateAgentToken();
    const auto second = generateAgentToken();
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    CHECK(first->size() == 64);
    CHECK(second->size() == 64);

    const auto is_lowercase_hex = [](std::string_view token) {
        return std::all_of(token.begin(), token.end(), [](char character) {
            return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
        });
    };
    CHECK(is_lowercase_hex(*first));
    CHECK(is_lowercase_hex(*second));
    CHECK(*first != *second);
}

TEST_CASE("Agent token comparison checks exact values", "[agent_protocol]") {
    const std::string token = "0123456789abcdef";
    CHECK(agentTokensEqual(token, token));

    std::string changed = token;
    changed[7] = 'f';
    CHECK_FALSE(agentTokensEqual(token, changed));

    CHECK_FALSE(agentTokensEqual(token, std::string_view{token}.substr(0, token.size() - 1)));
    CHECK_FALSE(agentTokensEqual(token, token + "0"));
}

namespace {

class ScopedScratchDirectory {
  public:
    ScopedScratchDirectory() {
        static std::atomic<unsigned long long> next{0};
        m_path = std::filesystem::temp_directory_path() /
                 ("rfsim-agent-protocol-" + test_temp_paths::processTag() + "-" +
                  std::to_string(next.fetch_add(1)));
        std::filesystem::create_directories(m_path);
    }

    ~ScopedScratchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path &path() const { return m_path; }

  private:
    std::filesystem::path m_path;
};

AgentEndpoint sampleEndpoint() {
    AgentEndpoint endpoint;
    endpoint.port = 43210;
    endpoint.bridge_token = std::string(64, 'a');
    endpoint.gui_token = std::string(64, 'b');
#ifdef _WIN32
    endpoint.pid = static_cast<long long>(GetCurrentProcessId());
#else
    endpoint.pid = static_cast<long long>(getpid());
#endif
    endpoint.app_version = "1.2.3";
    endpoint.catalog_version = 1;
    return endpoint;
}

#ifdef _WIN32
bool hasProtectedOwnerOnlyDacl(const std::filesystem::path &path, bool directory) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    DWORD token_size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_size);
    std::vector<std::byte> token_data(token_size);
    const bool token_ok =
        token_size != 0 &&
        GetTokenInformation(token, TokenUser, token_data.data(), token_size, &token_size);
    CloseHandle(token);
    if (!token_ok) {
        return false;
    }
    const PSID current_user = reinterpret_cast<const TOKEN_USER *>(token_data.data())->User.Sid;

    auto native_path = path.native();
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD security_result = GetNamedSecurityInfoW(
        native_path.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor);
    if (security_result != ERROR_SUCCESS) {
        return false;
    }

    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION acl_info{};
    bool valid = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                 (control & SE_DACL_PROTECTED) != 0 && owner != nullptr &&
                 EqualSid(owner, current_user) && dacl != nullptr &&
                 GetAclInformation(dacl, &acl_info, sizeof(acl_info), AclSizeInformation) &&
                 acl_info.AceCount == 1;
    if (valid) {
        void *ace = nullptr;
        valid = GetAce(dacl, 0, &ace) != FALSE;
        if (valid) {
            const auto *header = static_cast<const ACE_HEADER *>(ace);
            valid =
                header->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                header->AceFlags == (directory ? (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : 0);
            if (valid) {
                const auto *allow = static_cast<const ACCESS_ALLOWED_ACE *>(ace);
                valid = allow->Mask == FILE_ALL_ACCESS &&
                        EqualSid(const_cast<DWORD *>(&allow->SidStart), current_user);
            }
        }
    }
    LocalFree(descriptor);
    return valid;
}

std::string utf8FromWide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), length, nullptr,
                            nullptr) != length) {
        return {};
    }
    return result;
}
#endif

} // namespace

TEST_CASE("Agent install keys use the FNV-1a reference vectors and canonical executable directory",
          "[agent_protocol]") {
    CHECK(fnv1a64Hex("") == "cbf29ce484222325");
    CHECK(fnv1a64Hex("a") == "af63dc4c8601ec8c");
    CHECK(fnv1a64Hex("foobar") == "85944171f73967e8");

    ScopedScratchDirectory scratch;
    const auto canonical = std::filesystem::weakly_canonical(scratch.path());
#ifdef _WIN32
    auto native_path = canonical.native();
    const DWORD path_size = static_cast<DWORD>(native_path.size());
    REQUIRE(CharLowerBuffW(native_path.data(), path_size) == path_size);
    const std::string encoded_path = utf8FromWide(native_path);
#else
    const std::string encoded_path = canonical.native();
#endif
    const std::string install_key = agentInstallKey(scratch.path());
    REQUIRE(install_key.size() == 16);
    CHECK(install_key == fnv1a64Hex(encoded_path));
    CHECK(std::all_of(install_key.begin(), install_key.end(), [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    }));
    CHECK(agentEndpointFileName(install_key) ==
          std::filesystem::path("agent-endpoint-" + install_key + ".json"));
}

TEST_CASE("Agent endpoint round-trips the exact protocol JSON without a temp artifact",
          "[agent_protocol]") {
    ScopedScratchDirectory scratch;
    const auto directory = scratch.path() / "private";
    const auto file = directory / "agent-endpoint-test.json";
    std::string error;
    REQUIRE(ensurePrivateDirectory(directory, &error));

    const auto endpoint = sampleEndpoint();
    REQUIRE(writeAgentEndpoint(file, endpoint, &error));
    const auto loaded = readAgentEndpoint(file, &error);
    REQUIRE(loaded.has_value());
    CHECK(loaded->port == endpoint.port);
    CHECK(loaded->bridge_token == endpoint.bridge_token);
    CHECK(loaded->gui_token == endpoint.gui_token);
    CHECK(loaded->pid == endpoint.pid);
    CHECK(loaded->app_version == endpoint.app_version);
    CHECK(loaded->catalog_version == endpoint.catalog_version);

    std::ifstream input(file, std::ios::binary);
    REQUIRE(input.good());
    const auto json = nlohmann::json::parse(input);
    const std::set<std::string> expected_keys{
        "protocol", "port", "bridge_token", "gui_token", "pid", "app_version", "catalog_version"};
    std::set<std::string> actual_keys;
    for (const auto &[key, value] : json.items()) {
        static_cast<void>(value);
        actual_keys.insert(key);
    }
    CHECK(json.at("protocol") == "rfsim-agent/1");
    CHECK(actual_keys == expected_keys);

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        CHECK(entry.path() == file);
    }
}

TEST_CASE("Agent endpoint paths preserve non-ASCII directory names", "[agent_protocol]") {
    ScopedScratchDirectory scratch;
    const auto directory = scratch.path() / std::filesystem::path{u8"rf-sim-é-測試"};
    const auto endpoint_dir = directory / "private";
    const auto file = endpoint_dir / "agent-endpoint-test.json";
    std::string error;
    REQUIRE(ensurePrivateDirectory(endpoint_dir, &error));
    REQUIRE(writeAgentEndpoint(file, sampleEndpoint(), &error));

    const auto loaded = readAgentEndpoint(file, &error);
    REQUIRE(loaded.has_value());
    CHECK(loaded->gui_token == sampleEndpoint().gui_token);
}

TEST_CASE("Agent endpoint directories and files are owner-only", "[agent_protocol]") {
    ScopedScratchDirectory scratch;
    const auto directory = scratch.path() / "private";
    std::string error;
    REQUIRE(ensurePrivateDirectory(directory, &error));

#ifdef _WIN32
    CHECK(hasProtectedOwnerOnlyDacl(directory, true));
#else
    struct stat directory_status {};
    REQUIRE(stat(directory.c_str(), &directory_status) == 0);
    CHECK(directory_status.st_uid == geteuid());
    CHECK((directory_status.st_mode & 0777) == 0700);
#endif

    const auto endpoint_file = directory / "agent-endpoint-secure.json";
    REQUIRE(writeAgentEndpoint(endpoint_file, sampleEndpoint(), &error));
#ifdef _WIN32
    CHECK(hasProtectedOwnerOnlyDacl(endpoint_file, false));
#else
    struct stat file_status {};
    REQUIRE(stat(endpoint_file.c_str(), &file_status) == 0);
    CHECK(file_status.st_uid == geteuid());
    CHECK((file_status.st_mode & 0777) == 0600);
#endif

    const auto unsafe_directory = scratch.path() / "unsafe-directory";
    std::filesystem::create_directory(unsafe_directory);
#ifdef _WIN32
    CHECK_FALSE(ensurePrivateDirectory(unsafe_directory, &error));
#else
    REQUIRE(chmod(unsafe_directory.c_str(), 0755) == 0);
    CHECK_FALSE(ensurePrivateDirectory(unsafe_directory, &error));
#endif

    const auto unsafe_file = directory / "unsafe-endpoint.json";
    const auto unsafe_endpoint = sampleEndpoint();
    const nlohmann::json unsafe_json{{"protocol", "rfsim-agent/1"},
                                     {"port", unsafe_endpoint.port},
                                     {"bridge_token", unsafe_endpoint.bridge_token},
                                     {"gui_token", unsafe_endpoint.gui_token},
                                     {"pid", unsafe_endpoint.pid},
                                     {"app_version", unsafe_endpoint.app_version},
                                     {"catalog_version", unsafe_endpoint.catalog_version}};
    {
        std::ofstream output(unsafe_file, std::ios::binary);
        REQUIRE(output.good());
        output << unsafe_json.dump();
    }
#ifndef _WIN32
    REQUIRE(chmod(unsafe_file.c_str(), 0644) == 0);
#endif
    CHECK_FALSE(readAgentEndpoint(unsafe_file, &error).has_value());
    removeAgentEndpointIfOwned(unsafe_file, sampleEndpoint().gui_token);
    CHECK(std::filesystem::exists(unsafe_file));
}

TEST_CASE("Agent endpoint reads reject malformed JSON, fields, types, and ranges",
          "[agent_protocol]") {
    ScopedScratchDirectory scratch;
    const auto directory = scratch.path() / "private";
    const auto file = directory / "agent-endpoint-invalid.json";
    std::string error;
    REQUIRE(ensurePrivateDirectory(directory, &error));
    REQUIRE(writeAgentEndpoint(file, sampleEndpoint(), &error));

    std::ifstream input(file, std::ios::binary);
    REQUIRE(input.good());
    const auto valid = nlohmann::json::parse(input);
    input.close();

    auto wrong_protocol = valid;
    wrong_protocol["protocol"] = "rfsim-agent/2";
    auto wrong_type = valid;
    wrong_type["port"] = "43210";
    auto out_of_range = valid;
    out_of_range["port"] = 65536;
    auto extra_field = valid;
    extra_field["unexpected"] = true;
    auto missing_field = valid;
    missing_field.erase("gui_token");
    const std::vector<nlohmann::json> invalid_endpoints{wrong_protocol, wrong_type, out_of_range,
                                                        extra_field, missing_field};

    for (const auto &invalid : invalid_endpoints) {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << invalid.dump();
        output.close();
        REQUIRE(output.good());
        CHECK_FALSE(readAgentEndpoint(file, &error).has_value());
    }

    {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << "{";
    }
    CHECK_FALSE(readAgentEndpoint(file, &error).has_value());
}

TEST_CASE("Agent endpoint removal requires the matching GUI token", "[agent_protocol]") {
    ScopedScratchDirectory scratch;
    const auto directory = scratch.path() / "private";
    const auto file = directory / "agent-endpoint-owned.json";
    std::string error;
    REQUIRE(ensurePrivateDirectory(directory, &error));
    const auto endpoint = sampleEndpoint();
    REQUIRE(writeAgentEndpoint(file, endpoint, &error));

    CHECK_FALSE(removeAgentEndpointIfOwned(file, std::string(64, 'c')));
    CHECK(std::filesystem::exists(file));
    CHECK(removeAgentEndpointIfOwned(file, endpoint.gui_token));
    CHECK_FALSE(std::filesystem::exists(file));
}
