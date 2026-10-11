#include "session_state.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <windows.h>

namespace {

std::filesystem::path uniqueUnicodeScratchDirectory() {
    std::filesystem::path directory_name{u8"rf-sim-session-state-\U0001F9EA-"};
    directory_name += test_temp_paths::processTag();
    return std::filesystem::temp_directory_path() / directory_name;
}

class ScopedUnicodeScratchDirectory {
  public:
    ScopedUnicodeScratchDirectory() : m_path(uniqueUnicodeScratchDirectory()) {
        std::filesystem::create_directories(m_path);
    }

    ~ScopedUnicodeScratchDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path &path() const { return m_path; }

  private:
    std::filesystem::path m_path;
};

std::optional<std::string> distinguishingAcpSample(UINT active_acp) {
    const auto tryCharacter = [active_acp](wchar_t character) -> std::optional<std::string> {
        const int acp_length =
            WideCharToMultiByte(active_acp, 0, &character, 1, nullptr, 0, nullptr, nullptr);
        if (acp_length <= 0)
            return std::nullopt;

        std::string acp_bytes(static_cast<std::size_t>(acp_length), '\0');
        if (WideCharToMultiByte(active_acp, 0, &character, 1, acp_bytes.data(), acp_length, nullptr,
                                nullptr) != acp_length)
            return std::nullopt;

        const int round_trip_length =
            MultiByteToWideChar(active_acp, 0, acp_bytes.data(), acp_length, nullptr, 0);
        if (round_trip_length != 1)
            return std::nullopt;
        wchar_t round_trip = L'\0';
        if (MultiByteToWideChar(active_acp, 0, acp_bytes.data(), acp_length, &round_trip, 1) != 1 ||
            round_trip != character)
            return std::nullopt;

        const int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &character, 1,
                                                    nullptr, 0, nullptr, nullptr);
        if (utf8_length <= 0)
            return std::nullopt;
        std::string utf8_bytes(static_cast<std::size_t>(utf8_length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &character, 1, utf8_bytes.data(),
                                utf8_length, nullptr, nullptr) != utf8_length ||
            acp_bytes == utf8_bytes)
            return std::nullopt;

        return acp_bytes;
    };

    constexpr wchar_t preferred_characters[] = {L'\u00E9', L'\u00F1', L'\u20AC', L'\u0416',
                                                L'\u3042', L'\u4E2D', L'\u0E01', L'\uAC00'};
    for (const wchar_t character : preferred_characters)
        if (auto sample = tryCharacter(character))
            return sample;

    for (unsigned int code_point = 0x0080; code_point <= 0xFFFF; ++code_point) {
        if (code_point >= 0xD800 && code_point <= 0xDFFF)
            continue;
        if (auto sample = tryCharacter(static_cast<wchar_t>(code_point)))
            return sample;
    }
    return std::nullopt;
}

void writeAnsiIniFixture(const std::filesystem::path &path) {
    std::ofstream fixture(path, std::ios::binary | std::ios::trunc);
    REQUIRE(fixture.is_open());
    fixture << "[LegacySessionState]\r\nSeed=ascii\r\n";
    REQUIRE(fixture.good());
    fixture.close();
    REQUIRE_FALSE(fixture.fail());
}

} // namespace

TEST_CASE("SessionState preserves legacy ACP text in an ANSI INI under a Unicode path",
          "[session][unicode][ansi]") {
    ScopedUnicodeScratchDirectory scratch;
    const auto app_ini = scratch.path() / "app.ini";
    writeAnsiIniFixture(app_ini);
    REQUIRE(std::filesystem::exists(app_ini));

    const UINT active_acp = GetACP();
    if (active_acp == CP_UTF8)
        SKIP("active Windows ACP is UTF-8; no distinct ACP-versus-UTF-8 sample exists");
    const auto value = distinguishingAcpSample(active_acp);
    if (!value)
        SKIP("active Windows ACP has no representable non-ASCII sample distinct from UTF-8");

    SessionState writer{app_ini};
    writer.save("LegacySessionState", "NonAsciiValue", value->c_str());
    CHECK(writer.fileExists());

    SessionState reader{app_ini};
    CHECK(reader.load("LegacySessionState", "NonAsciiValue", "fallback") == *value);
}

TEST_CASE("SessionState round-trips app.ini under a non-ANSI Unicode directory",
          "[session][unicode]") {
    ScopedUnicodeScratchDirectory scratch;
    const auto app_ini = scratch.path() / "app.ini";

    SessionState writer{app_ini};
    writer.save("UnicodePathRegression", "AgentEnabled", "1");

    REQUIRE(std::filesystem::exists(app_ini));
    CHECK(writer.fileExists());

    SessionState reader{app_ini};
    CHECK(reader.load("UnicodePathRegression", "AgentEnabled", "0") == "1");
}
