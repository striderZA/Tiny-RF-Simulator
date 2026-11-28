#include "test_temp_paths.h"
#include "update_check.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr const char *kNewerRelease = R"json({
  "tag_name": "v0.33.0",
  "html_url": "https://github.com/striderZA/Tiny-RF-Simulator/releases/tag/v0.33.0",
  "assets": [{"name": "rf-simulator-linux-x86_64.tar.gz"}]
})json";

class TempTree {
  public:
    TempTree() {
        static std::atomic<unsigned> sequence{0};
        root = fs::temp_directory_path() / ("rfsim_update_check_" + test_temp_paths::processTag() +
                                            "_" + std::to_string(sequence.fetch_add(1)));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }

    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path root;
};

class ScopedSearchPath {
  public:
    explicit ScopedSearchPath(const fs::path &directory) {
        if (const char *value = std::getenv("PATH"))
            m_original = value;
#ifdef _WIN32
        REQUIRE(_putenv_s("PATH", directory.string().c_str()) == 0);
#else
        REQUIRE(setenv("PATH", directory.string().c_str(), 1) == 0);
#endif
    }

    ~ScopedSearchPath() {
#ifdef _WIN32
        _putenv_s("PATH", m_original ? m_original->c_str() : "");
#else
        if (m_original)
            setenv("PATH", m_original->c_str(), 1);
        else
            unsetenv("PATH");
#endif
    }

  private:
    std::optional<std::string> m_original;
};

simulator::update_check::Result evaluate(std::string_view json, std::string_view current) {
    return simulator::update_check::evaluateReleaseJson(json, current);
}

bool contains(const std::vector<std::string> &lines, std::string_view text) {
    for (const std::string &line : lines) {
        if (line.find(text) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("Update version parses leading v, pre-release, and build suffixes") {
    const auto plain = simulator::update_check::parseVersion("0.32.0");
    REQUIRE(plain.has_value());
    CHECK(plain->major == 0);
    CHECK(plain->minor == 32);
    CHECK(plain->patch == 0);
    CHECK(plain->prerelease.empty());

    const auto tagged = simulator::update_check::parseVersion("v1.2.3");
    REQUIRE(tagged.has_value());
    CHECK(tagged->major == 1);
    CHECK(tagged->minor == 2);
    CHECK(tagged->patch == 3);

    const auto prerelease = simulator::update_check::parseVersion("v0.33.0-rc.1");
    REQUIRE(prerelease.has_value());
    CHECK(prerelease->prerelease == "rc.1");

    const auto build = simulator::update_check::parseVersion("v0.33.0+build.5");
    REQUIRE(build.has_value());
    CHECK(build->prerelease.empty());
}

TEST_CASE("Update version rejects malformed tags") {
    for (const char *text : {"0.33", "0.33.0.1", "v", "foo", "1.2.x", "v1.2.3-", "1..3"}) {
        INFO(text);
        CHECK_FALSE(simulator::update_check::parseVersion(text).has_value());
    }
}

TEST_CASE("Update version comparison orders releases and pre-releases") {
    const auto version = [](const char *text) {
        const auto parsed = simulator::update_check::parseVersion(text);
        REQUIRE(parsed.has_value());
        return *parsed;
    };
    using simulator::update_check::compareVersions;

    CHECK(compareVersions(version("v0.33.0"), version("0.32.0")) > 0);
    CHECK(compareVersions(version("v0.31.9"), version("0.32.0")) < 0);
    CHECK(compareVersions(version("v0.32.0"), version("0.32.0")) == 0);
    CHECK(compareVersions(version("v0.33.0-rc.1"), version("v0.32.0")) > 0);
    CHECK(compareVersions(version("v0.33.0-rc.1"), version("v0.33.0")) < 0);
    CHECK(compareVersions(version("v0.33.0-rc.1"), version("v0.33.0-rc.2")) < 0);
    CHECK(compareVersions(version("v0.33.0-beta"), version("v0.33.0-rc")) < 0);
    CHECK(compareVersions(version("v0.33.0-alpha"), version("v0.33.0-alpha.1")) < 0);
}

TEST_CASE("Update release JSON reads the tag and page") {
    std::string error;
    const auto release = simulator::update_check::parseLatestReleaseJson(kNewerRelease, error);
    REQUIRE(release.has_value());
    CHECK(release->tag == "v0.33.0");
    CHECK(release->html_url ==
          "https://github.com/striderZA/Tiny-RF-Simulator/releases/tag/v0.33.0");
    CHECK(error.empty());
}

TEST_CASE("Update release JSON reports malformed payloads") {
    std::string error;
    CHECK_FALSE(simulator::update_check::parseLatestReleaseJson("{not json", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(
        simulator::update_check::parseLatestReleaseJson(R"json({"html_url": "x"})json", error)
            .has_value());
    CHECK(error.find("tag") != std::string::npos);

    error.clear();
    CHECK_FALSE(
        simulator::update_check::parseLatestReleaseJson(R"json({"tag_name": "v0.33.0"})json", error)
            .has_value());
    CHECK(error.find("URL") != std::string::npos);
}

TEST_CASE("Update evaluation reports newest, available, older, and pre-release") {
    using simulator::update_check::Outcome;

    const auto newer = evaluate(kNewerRelease, "0.32.0");
    CHECK(newer.outcome == Outcome::UpdateAvailable);
    CHECK(newer.latest_tag == "v0.33.0");
    CHECK(newer.release_url.find("/releases/tag/v0.33.0") != std::string::npos);
    const auto newer_lines = simulator::update_check::statusLines(newer);
    CHECK(contains(newer_lines, "v0.33.0 is available."));
    CHECK(contains(newer_lines, "You have 0.32.0."));
    CHECK(contains(newer_lines, "/releases/tag/v0.33.0"));

    const auto equal = evaluate(kNewerRelease, "0.33.0");
    CHECK(equal.outcome == Outcome::UpToDate);
    CHECK(contains(simulator::update_check::statusLines(equal),
                   "You have the newest version (0.33.0)."));

    const auto older = evaluate(kNewerRelease, "0.34.0");
    CHECK(older.outcome == Outcome::UpToDate);

    const auto prerelease = evaluate(R"json({"tag_name": "v0.33.0-rc.1",
                          "html_url": "https://example.test/prerelease"})json",
                                     "0.32.0");
    CHECK(prerelease.outcome == Outcome::UpdateAvailable);
    CHECK(prerelease.latest_tag == "v0.33.0-rc.1");
}

TEST_CASE("Update evaluation fails loudly on malformed releases") {
    using simulator::update_check::Outcome;

    for (const char *payload : {"{not json", R"json({"html_url": "x"})json",
                                R"json({"tag_name": "nightly", "html_url": "x"})json",
                                R"json({"tag_name": "v0.33", "html_url": "x"})json"}) {
        INFO(payload);
        const auto result = evaluate(payload, "0.32.0");
        CHECK(result.outcome == Outcome::Failed);
        CHECK_FALSE(result.message.empty());
        CHECK(contains(simulator::update_check::statusLines(result), "Update check failed:"));
    }
}

TEST_CASE("Update check reports a missing curl and wget instead of failing silently") {
    TempTree tree;
    ScopedSearchPath empty_path(tree.root);

    const auto fetched = simulator::update_check::fetchLatestReleaseJson();
    CHECK_FALSE(fetched.ok);
    CHECK(fetched.body.empty());
    CHECK(fetched.error.find("Neither curl nor wget") != std::string::npos);
}

TEST_CASE("Update checker starts idle, runs a fetcher off the caller, and reaps it") {
    using simulator::update_check::FetchResult;
    using simulator::update_check::Outcome;

    simulator::update_check::UpdateChecker checker("0.32.0");
    CHECK(checker.result().outcome == Outcome::Idle);

    checker.setFetcherForTesting([]() { return FetchResult{true, kNewerRelease, ""}; });
    checker.start();
    CHECK(checker.isChecking());

    for (int attempt = 0; attempt < 200 && checker.isChecking(); ++attempt) {
        checker.poll();
        if (checker.isChecking())
            std::this_thread::sleep_for(5ms);
    }
    REQUIRE_FALSE(checker.isChecking());
    CHECK(checker.result().outcome == Outcome::UpdateAvailable);
    CHECK(checker.result().latest_tag == "v0.33.0");

    checker.reset();
    CHECK(checker.result().outcome == Outcome::Idle);
}

TEST_CASE("Update checker surfaces a fetch failure") {
    using simulator::update_check::FetchResult;
    using simulator::update_check::Outcome;

    simulator::update_check::UpdateChecker checker("0.32.0");
    checker.setFetcherForTesting(
        []() { return FetchResult{false, "", "curl exited with code 6."}; });
    checker.start();
    for (int attempt = 0; attempt < 200 && checker.isChecking(); ++attempt) {
        checker.poll();
        if (checker.isChecking())
            std::this_thread::sleep_for(5ms);
    }

    REQUIRE_FALSE(checker.isChecking());
    CHECK(checker.result().outcome == Outcome::Failed);
    CHECK(checker.result().message.find("curl exited with code 6.") != std::string::npos);
}

TEST_CASE("Update checker destruction never waits for a slow fetch") {
    using simulator::update_check::FetchResult;

    const auto started = std::chrono::steady_clock::now();
    {
        simulator::update_check::UpdateChecker checker("0.32.0");
        checker.setFetcherForTesting([]() {
            std::this_thread::sleep_for(1s);
            return FetchResult{true, kNewerRelease, ""};
        });
        checker.start();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < 300ms);
}
