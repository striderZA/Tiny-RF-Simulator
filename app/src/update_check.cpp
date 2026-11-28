#include "update_check.h"

#include "process_capture.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace simulator::update_check {

namespace {

constexpr std::size_t kMaxResponseBytes = 1024 * 1024;
constexpr auto kFetchTimeout = std::chrono::seconds(20);

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

bool parseNumber(std::string_view text, int &value) {
    if (text.empty())
        return false;
    const auto *begin = text.data();
    const auto *end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end && value >= 0;
}

bool isNumeric(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return character >= '0' && character <= '9';
    });
}

int comparePrerelease(std::string_view left, std::string_view right) {
    if (left.empty() || right.empty())
        return left.empty() == right.empty() ? 0 : (left.empty() ? 1 : -1);

    std::size_t left_begin = 0;
    std::size_t right_begin = 0;
    while (left_begin < left.size() || right_begin < right.size()) {
        const std::size_t left_end = left.find('.', left_begin);
        const std::size_t right_end = right.find('.', right_begin);
        const std::string_view left_part =
            left.substr(left_begin, left_end == std::string_view::npos ? std::string_view::npos
                                                                       : left_end - left_begin);
        const std::string_view right_part = right.substr(
            right_begin,
            right_end == std::string_view::npos ? std::string_view::npos : right_end - right_begin);
        if (left_part.empty() || right_part.empty()) {
            if (left_part.empty() && right_part.empty())
                return 0;
            return left_part.empty() ? -1 : 1;
        }
        const bool left_numeric = isNumeric(left_part);
        const bool right_numeric = isNumeric(right_part);
        if (left_numeric && right_numeric) {
            long long left_value = 0;
            long long right_value = 0;
            const auto left_parsed =
                std::from_chars(left_part.data(), left_part.data() + left_part.size(), left_value);
            const auto right_parsed = std::from_chars(
                right_part.data(), right_part.data() + right_part.size(), right_value);
            if (left_parsed.ec == std::errc{} && right_parsed.ec == std::errc{} &&
                left_value != right_value)
                return left_value < right_value ? -1 : 1;
        } else if (left_numeric != right_numeric) {
            return left_numeric ? -1 : 1;
        } else if (left_part != right_part) {
            return left_part < right_part ? -1 : 1;
        }
        left_begin = left_end == std::string_view::npos ? left.size() : left_end + 1;
        right_begin = right_end == std::string_view::npos ? right.size() : right_end + 1;
    }
    return 0;
}

std::string toolNotFoundMessage() {
    return "Neither curl nor wget was found on PATH. Install one, or download the release from "
           "GitHub.";
}

} // namespace

std::optional<Version> parseVersion(std::string_view text) {
    text = trim(text);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    if (text.empty())
        return std::nullopt;

    const std::size_t build_start = text.find('+');
    const std::string_view without_build =
        build_start == std::string_view::npos ? text : text.substr(0, build_start);
    const std::size_t prerelease_start = without_build.find('-');
    const std::string_view core = prerelease_start == std::string_view::npos
                                      ? without_build
                                      : without_build.substr(0, prerelease_start);

    Version version;
    std::size_t begin = 0;
    int *parts[3] = {&version.major, &version.minor, &version.patch};
    for (int index = 0; index < 3; ++index) {
        const std::size_t end = core.find('.', begin);
        const std::string_view part = core.substr(
            begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        if (!parseNumber(part, *parts[index]))
            return std::nullopt;
        if (index < 2) {
            if (end == std::string_view::npos)
                return std::nullopt;
            begin = end + 1;
        } else if (end != std::string_view::npos) {
            return std::nullopt;
        }
    }

    if (prerelease_start != std::string_view::npos) {
        version.prerelease = std::string(without_build.substr(prerelease_start + 1));
        if (version.prerelease.empty())
            return std::nullopt;
        std::size_t part_begin = 0;
        while (part_begin <= version.prerelease.size()) {
            const std::size_t part_end = version.prerelease.find('.', part_begin);
            const std::size_t length =
                part_end == std::string::npos ? std::string::npos : part_end - part_begin;
            if (version.prerelease.substr(part_begin, length).empty())
                return std::nullopt;
            if (part_end == std::string::npos)
                break;
            part_begin = part_end + 1;
        }
    }
    return version;
}

int compareVersions(const Version &left, const Version &right) {
    if (left.major != right.major)
        return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor)
        return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch)
        return left.patch < right.patch ? -1 : 1;
    return comparePrerelease(left.prerelease, right.prerelease);
}

std::optional<ReleaseInfo> parseLatestReleaseJson(std::string_view json, std::string &error) {
    error.clear();
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "The release response was not valid JSON.";
        return std::nullopt;
    }

    const auto tag = root.find("tag_name");
    if (tag == root.end() || !tag->is_string() || tag->get<std::string>().empty()) {
        error = "The release response has no tag name.";
        return std::nullopt;
    }
    const auto url = root.find("html_url");
    if (url == root.end() || !url->is_string() || url->get<std::string>().empty()) {
        error = "The release response has no release URL.";
        return std::nullopt;
    }
    return ReleaseInfo{tag->get<std::string>(), url->get<std::string>()};
}

Result failedResult(std::string_view current_version, std::string message) {
    Result result;
    result.outcome = Outcome::Failed;
    result.current_version = std::string(current_version);
    result.message = std::move(message);
    return result;
}

Result evaluateReleaseJson(std::string_view json, std::string_view current_version) {
    const auto running = parseVersion(current_version);
    if (!running)
        return failedResult(current_version, "This build's version is not a version number: " +
                                                 std::string(current_version) + ".");

    std::string error;
    const auto release = parseLatestReleaseJson(json, error);
    if (!release)
        return failedResult(current_version,
                            "The update check could not read the release: " + error);

    const auto latest = parseVersion(release->tag);
    if (!latest)
        return failedResult(current_version, "The latest release tag '" + release->tag +
                                                 "' is not a version number.");

    Result result;
    result.current_version = std::string(current_version);
    result.latest_tag = release->tag;
    result.release_url = release->html_url;
    result.outcome =
        compareVersions(*latest, *running) > 0 ? Outcome::UpdateAvailable : Outcome::UpToDate;
    return result;
}

std::vector<std::string> statusLines(const Result &result) {
    switch (result.outcome) {
    case Outcome::Idle:
        return {"Click Check again to look for a newer release."};
    case Outcome::Checking:
        return {"Checking for the latest release..."};
    case Outcome::UpToDate:
        return {"You have the newest version (" + result.current_version + ")."};
    case Outcome::UpdateAvailable:
        return {result.latest_tag + " is available.", "You have " + result.current_version + ".",
                "Release page: " + result.release_url};
    case Outcome::Failed:
        return {"Update check failed: " + result.message,
                "The app never checks for updates unless you click Check for Updates."};
    }
    return {};
}

FetchResult fetchLatestReleaseJson() {
    const auto curl = process_capture::findExecutable("curl");
    const auto wget =
        curl ? std::optional<std::filesystem::path>() : process_capture::findExecutable("wget");
    if (!curl && !wget)
        return FetchResult{false, "", toolNotFoundMessage()};

    const std::string tool = curl ? "curl" : "wget";
    std::vector<std::string> arguments;
    if (curl) {
        arguments = {"-fsSL",
                     "--max-time",
                     "10",
                     "--connect-timeout",
                     "5",
                     "-H",
                     "Accept: application/vnd.github+json",
                     std::string(kLatestReleaseUrl)};
    } else {
        arguments = {"-q", "-O", "-", "--timeout=10", "--tries=1", std::string(kLatestReleaseUrl)};
    }

    process_capture::Options options;
    options.timeout = kFetchTimeout;
    options.max_output_bytes = kMaxResponseBytes;
    const auto result = process_capture::run(tool, arguments, options);
    if (!result || !result->started)
        return FetchResult{false, "", "Could not start " + tool + "."};
    if (result->timed_out)
        return FetchResult{false, "", tool + " timed out while checking for updates."};
    if (result->output_truncated)
        return FetchResult{false, "", "The update check response was too large."};
    if (result->exit_code != 0)
        return FetchResult{false, "",
                           tool + " exited with code " + std::to_string(result->exit_code) + "."};
    return FetchResult{true, result->output, ""};
}

UpdateChecker::UpdateChecker(std::string current_version, Fetcher fetcher)
    : m_current_version(std::move(current_version)), m_fetcher(std::move(fetcher)) {
    m_result.current_version = m_current_version;
}

UpdateChecker::~UpdateChecker() {
    if (m_future.valid())
        m_future.wait();
}

void UpdateChecker::setFetcherForTesting(Fetcher fetcher) { m_fetcher = std::move(fetcher); }

void UpdateChecker::start() {
    if (isChecking())
        return;
    if (m_future.valid())
        m_future.wait();
    m_result = Result{};
    m_result.outcome = Outcome::Checking;
    m_result.current_version = m_current_version;
    const Fetcher fetcher = m_fetcher;
    m_future = std::async(std::launch::async, [fetcher]() { return fetcher(); });
}

void UpdateChecker::poll() {
    if (!m_future.valid() ||
        m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    FetchResult fetched;
    try {
        fetched = m_future.get();
    } catch (const std::exception &error) {
        fetched = FetchResult{false, "",
                              std::string("The update check failed unexpectedly: ") + error.what()};
    } catch (...) {
        fetched = FetchResult{false, "", "The update check failed unexpectedly."};
    }
    finish(std::move(fetched));
}

void UpdateChecker::reset() {
    if (m_future.valid())
        m_future.wait();
    m_future = std::future<FetchResult>{};
    m_result = Result{};
    m_result.current_version = m_current_version;
}

void UpdateChecker::finish(FetchResult fetched) {
    m_future = std::future<FetchResult>{};
    if (!fetched.ok) {
        m_result =
            failedResult(m_current_version, fetched.error.empty() ? "The update check failed."
                                                                  : std::move(fetched.error));
        return;
    }
    m_result = evaluateReleaseJson(fetched.body, m_current_version);
}

} // namespace simulator::update_check
