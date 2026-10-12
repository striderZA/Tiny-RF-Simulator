#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

struct GitStatus {
    std::string branch;
    std::string commit;
    std::size_t staged_count = 0;
    std::size_t unstaged_count = 0;
    std::size_t untracked_count = 0;
    std::size_t conflicted_count = 0;

    std::string label() const;
    std::string details() const;
};

// Truncates text at a UTF-8 code-point boundary to fit a measured width.
// Returns an empty string when even the ellipsis does not fit.
std::string fitTextToWidth(std::string_view text, float max_width,
                           const std::function<float(std::string_view)> &measure);

// Keeps the commit and change counts visible before shortening the branch name.
std::string fitGitStatusHeaderLabel(const GitStatus &status, float max_width,
                                    const std::function<float(std::string_view)> &measure);

struct GitStatusHeaderLabels {
    std::string project_title;
    std::string git_title;
};

// Fits the project and Git labels while reserving room for the FPS and the
// complete commit/change summary before shortening branch text.
GitStatusHeaderLabels fitGitHeaderLabels(std::string_view project_title,
                                         const std::optional<GitStatus> &status,
                                         float available_width, float fps_width, float item_spacing,
                                         const std::function<float(std::string_view)> &measure);

// Parses the machine-readable `git status --porcelain=v2 --branch` response.
// An absent or malformed branch identity returns nullopt.
std::optional<GitStatus> parseGitStatus(std::string_view output);

// Queries the repository containing working_directory. Git is invoked directly,
// without a shell; command or repository failures return nullopt.
std::optional<GitStatus> queryGitStatus(const std::filesystem::path &working_directory);

class GitStatusMonitor {
  public:
    static constexpr std::chrono::seconds kRefreshInterval{2};

    // Starts or polls a background refresh on first call, directory changes, or
    // every two seconds. Returns the last completed snapshot without waiting.
    const std::optional<GitStatus> &update(const std::filesystem::path &working_directory);
    const std::optional<GitStatus> &status() const { return m_status; }
    std::string label() const;
    std::string details() const;

  private:
    static constexpr std::size_t kMaxConcurrentQueries = 4;
    struct PendingQuery {
        std::filesystem::path directory;
        std::size_t generation = 0;
        std::future<std::optional<GitStatus>> future;
    };
    std::filesystem::path m_working_directory;
    std::vector<PendingQuery> m_pending_queries;
    std::optional<GitStatus> m_status;
    std::size_t m_generation = 0;
    std::chrono::steady_clock::time_point m_last_refresh{};
    bool m_has_refreshed = false;
};
