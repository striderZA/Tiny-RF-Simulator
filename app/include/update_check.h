#pragma once

#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simulator::update_check {

// The release API endpoint the app queries. The app never calls it on its own.
inline constexpr std::string_view kLatestReleaseUrl =
    "https://api.github.com/repos/striderZA/Tiny-RF-Simulator/releases/latest";

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    // The `-prerelease` identifiers without the leading dash; empty for a release.
    std::string prerelease;
};

// Parses `X.Y.Z`, an optional leading `v`/`V`, an optional `-prerelease` and an
// ignored `+build` suffix. Returns nullopt for anything else.
std::optional<Version> parseVersion(std::string_view text);

// Negative when `left` is older, 0 when equal, positive when newer. A release
// outranks a pre-release of the same X.Y.Z.
int compareVersions(const Version &left, const Version &right);

struct ReleaseInfo {
    std::string tag;
    std::string html_url;
};

// Reads the fields the update check uses from GitHub's latest-release JSON.
// A non-object, missing/empty tag, or missing/empty URL is reported in `error`.
std::optional<ReleaseInfo> parseLatestReleaseJson(std::string_view json, std::string &error);

enum class Outcome { Idle, Checking, UpToDate, UpdateAvailable, Failed };

struct Result {
    Outcome outcome = Outcome::Idle;
    std::string current_version;
    std::string latest_tag;
    std::string release_url;
    // The failure reason, when outcome is Failed.
    std::string message;
};

// Evaluates a release payload against the running version. A malformed payload,
// an unreadable tag, or a tag that is not X.Y.Z becomes Failed with a reason.
Result evaluateReleaseJson(std::string_view json, std::string_view current_version);
Result failedResult(std::string_view current_version, std::string message);

// The lines the Check for Updates window draws for this result.
std::vector<std::string> statusLines(const Result &result);

struct FetchResult {
    bool ok = false;
    std::string body;
    std::string error;
};

// Runs curl, or wget when curl is absent, against kLatestReleaseUrl. The call
// blocks, so it must run on a worker thread; a missing tool is reported as a
// failure, never a silent empty result.
FetchResult fetchLatestReleaseJson();

using Fetcher = std::function<FetchResult()>;

// Owns one user-triggered check. start() launches the fetch, poll() reaps it
// without blocking, and result() reports the latest state. Nothing checks on
// construction or on a timer.
class UpdateChecker {
  public:
    explicit UpdateChecker(std::string current_version, Fetcher fetcher = fetchLatestReleaseJson);
    ~UpdateChecker();

    UpdateChecker(const UpdateChecker &) = delete;
    UpdateChecker &operator=(const UpdateChecker &) = delete;

    // Intended for tests; set it before start().
    void setFetcherForTesting(Fetcher fetcher);

    void start();
    void poll();
    bool isChecking() const { return m_result.outcome == Outcome::Checking; }
    const Result &result() const { return m_result; }
    // Back to Idle, so a test can drive several checks in one app session.
    void reset();

  private:
    void finish(FetchResult fetched);

    std::string m_current_version;
    Fetcher m_fetcher;
    std::future<FetchResult> m_future;
    Result m_result;
};

} // namespace simulator::update_check
