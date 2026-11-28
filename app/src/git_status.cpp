#include "git_status.h"

#include "process_capture.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxStatusOutputBytes = 4 * 1024 * 1024;
constexpr auto kGitStatusProcessTimeout = std::chrono::seconds(1);

std::optional<std::string> runGitStatus(const std::filesystem::path &working_directory) {
    simulator::process_capture::Options options;
    options.working_directory = working_directory;
    options.timeout = kGitStatusProcessTimeout;
    options.max_output_bytes = kMaxStatusOutputBytes;
    options.remove_environment_names = {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR",
                                        "GIT_INDEX_FILE"};
    const auto result = simulator::process_capture::run(
        "git",
        {"-c", "core.fsmonitor=false", "--no-optional-locks", "status", "--porcelain=v2",
         "--branch", "--untracked-files=normal"},
        options);
    if (!result || !result->started || result->timed_out || result->output_truncated ||
        result->exit_code != 0)
        return std::nullopt;
    return result->output;
}

bool isHexHash(std::string_view value) {
    return value.size() >= 7 &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return std::isxdigit(character) != 0; });
}

} // namespace

std::optional<GitStatus> parseGitStatus(std::string_view output) {
    constexpr std::string_view kOidPrefix = "# branch.oid ";
    constexpr std::string_view kHeadPrefix = "# branch.head ";

    GitStatus status;
    bool has_oid = false;
    bool has_head = false;
    std::size_t begin = 0;
    while (begin < output.size()) {
        const std::size_t end = output.find('\n', begin);
        std::string_view line = output.substr(
            begin, end == std::string_view::npos ? output.size() - begin : end - begin);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (line.starts_with(kOidPrefix)) {
            const std::string_view oid = line.substr(kOidPrefix.size());
            if (oid == "(initial)") {
                status.commit.clear();
            } else {
                if (!isHexHash(oid))
                    return std::nullopt;
                status.commit = std::string(oid.substr(0, 7));
            }
            has_oid = true;
        } else if (line.starts_with(kHeadPrefix)) {
            status.branch = std::string(line.substr(kHeadPrefix.size()));
            if (status.branch == "(detached)")
                status.branch = "detached";
            has_head = !status.branch.empty();
        } else if (line.starts_with("? ")) {
            ++status.untracked_count;
        } else if (line.size() >= 4 && line[1] == ' ' && line[0] == 'u') {
            ++status.conflicted_count;
        } else if (line.size() >= 4 && line[1] == ' ' && (line[0] == '1' || line[0] == '2')) {
            const char index_status = line[2];
            const char worktree_status = line[3];
            if (index_status != '.')
                ++status.staged_count;
            if (worktree_status != '.')
                ++status.unstaged_count;
        }

        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }

    if (!has_oid || !has_head)
        return std::nullopt;
    return status;
}

// Git only reports "not a git repository" in a plain folder, so it is not started
// unless the directory or an ancestor holds a .git entry (a directory, or a file
// for worktrees and submodules).
static bool isInsideGitWorkTree(const std::filesystem::path &directory) {
    std::error_code error;
    for (auto current = directory;; current = current.parent_path()) {
        if (std::filesystem::exists(current / ".git", error))
            return true;
        if (current == current.parent_path())
            return false;
    }
}

std::optional<GitStatus> queryGitStatus(const std::filesystem::path &working_directory) {
    if (working_directory.empty())
        return std::nullopt;
    std::error_code error;
    const std::filesystem::path absolute_directory =
        std::filesystem::absolute(working_directory, error).lexically_normal();
    if (error || !std::filesystem::is_directory(absolute_directory, error) || error)
        return std::nullopt;
    // Plain folders: skip the git process, which would only report "not a git repository".
    if (!isInsideGitWorkTree(absolute_directory))
        return std::nullopt;

    const auto output = runGitStatus(absolute_directory);
    if (!output)
        return std::nullopt;
    return parseGitStatus(*output);
}

std::string GitStatus::label() const {
    std::string result = branch.empty() ? "HEAD" : branch;
    result += '@';
    result += commit.empty() ? "(initial)" : commit;
    if (staged_count != 0)
        result += " +" + std::to_string(staged_count);
    if (unstaged_count != 0)
        result += " ~" + std::to_string(unstaged_count);
    if (untracked_count != 0)
        result += " ?" + std::to_string(untracked_count);
    if (conflicted_count != 0)
        result += " !" + std::to_string(conflicted_count);
    return result;
}

std::string GitStatus::details() const {
    std::string result = "Branch: " + (branch.empty() ? "HEAD" : branch);
    result += "\nCommit: ";
    result += commit.empty() ? "(initial)" : commit;
    result += "\nStaged paths: " + std::to_string(staged_count);
    result += "\nUnstaged paths: " + std::to_string(unstaged_count);
    result += "\nUntracked paths: " + std::to_string(untracked_count);
    result += "\nConflicted paths: " + std::to_string(conflicted_count);
    if (staged_count == 0 && unstaged_count == 0 && untracked_count == 0 && conflicted_count == 0)
        result += "\nWorking tree: clean";
    return result;
}

std::string fitTextToWidth(std::string_view text, float max_width,
                           const std::function<float(std::string_view)> &measure) {
    constexpr std::string_view ellipsis = "...";
    if (text.empty() || max_width <= 0.0f)
        return {};
    if (measure(text) <= max_width)
        return std::string(text);
    if (measure(ellipsis) > max_width)
        return {};

    std::size_t visible_bytes = 0;
    for (std::size_t end = 0; end < text.size();) {
        const auto lead = static_cast<unsigned char>(text[end]);
        const std::size_t code_point_bytes = lead < 0x80   ? 1
                                             : lead < 0xE0 ? 2
                                             : lead < 0xF0 ? 3
                                                           : 4;
        if (end + code_point_bytes > text.size())
            break;
        end += code_point_bytes;
        std::string candidate(text.substr(0, end));
        candidate += ellipsis;
        if (measure(candidate) > max_width)
            break;
        visible_bytes = end;
    }

    std::string result(text.substr(0, visible_bytes));
    result += ellipsis;
    return result;
}

namespace {

std::string gitStatusHeaderSummary(const GitStatus &status) {
    std::string result = "Git: ";
    result += status.commit.empty() ? "(initial)" : status.commit;
    const auto append_count = [&result](char marker, std::size_t count) {
        if (count != 0) {
            result += ' ';
            result += marker;
            result += std::to_string(count);
        }
    };
    append_count('+', status.staged_count);
    append_count('~', status.unstaged_count);
    append_count('?', status.untracked_count);
    append_count('!', status.conflicted_count);
    return result;
}

} // namespace

std::string fitGitStatusHeaderLabel(const GitStatus &status, float max_width,
                                    const std::function<float(std::string_view)> &measure) {
    const std::string commit_part =
        "Git: " + (status.commit.empty() ? std::string("(initial)") : status.commit);
    const std::string summary = gitStatusHeaderSummary(status);
    const std::string counts = summary.substr(commit_part.size());
    std::string primary = summary;
    if (measure(primary) > max_width) {
        if (measure(commit_part) > max_width)
            return fitTextToWidth(commit_part, max_width, measure);
        const float counts_width = max_width - measure(commit_part);
        const std::string fitted_counts = fitTextToWidth(counts, counts_width, measure);
        const bool shows_count = fitted_counts.find_first_of("0123456789") != std::string::npos;
        primary = shows_count ? commit_part + fitted_counts : commit_part;
        if (measure(primary) > max_width)
            primary = commit_part;
    }

    const std::string branch = status.branch.empty() ? "" : " " + status.branch;
    if (measure(primary + branch) <= max_width)
        return primary + branch;
    if (branch.empty())
        return primary;
    const float branch_width = max_width - measure(primary);
    const std::string fitted_branch = fitTextToWidth(branch, branch_width, measure);
    const std::size_t first_branch_byte = fitted_branch.find_first_not_of(' ');
    if (first_branch_byte == std::string::npos || fitted_branch.substr(first_branch_byte) == "...")
        return primary;
    if (measure(primary + fitted_branch) <= max_width)
        return primary + fitted_branch;
    return primary;
}

GitStatusHeaderLabels fitGitHeaderLabels(std::string_view project_title,
                                         const std::optional<GitStatus> &status,
                                         float available_width, float right_labels_width,
                                         float item_spacing,
                                         const std::function<float(std::string_view)> &measure) {
    constexpr std::string_view unavailable_label = "Git unavailable";
    const float minimum_git_width =
        status ? measure(gitStatusHeaderSummary(*status)) : measure(unavailable_label);
    const float project_gap_count = project_title.empty() ? 1.0f : 2.0f;
    const float project_width_budget =
        std::max(0.0f, available_width - right_labels_width - item_spacing * project_gap_count -
                           minimum_git_width);

    GitStatusHeaderLabels labels;
    labels.project_title = fitTextToWidth(project_title, project_width_budget, measure);
    const bool show_project_title = !labels.project_title.empty();
    const float gap_count = show_project_title ? 2.0f : 1.0f;
    const float git_width =
        std::max(0.0f, available_width - right_labels_width - item_spacing * gap_count -
                           (show_project_title ? measure(labels.project_title) : 0.0f));
    labels.git_title = status ? fitGitStatusHeaderLabel(*status, git_width, measure)
                              : fitTextToWidth(unavailable_label, git_width, measure);
    return labels;
}

const std::optional<GitStatus> &
GitStatusMonitor::update(const std::filesystem::path &working_directory) {
    const auto normalized_directory = working_directory.lexically_normal();
    const auto now = std::chrono::steady_clock::now();
    if (normalized_directory != m_working_directory) {
        m_working_directory = normalized_directory;
        m_status.reset();
        m_has_refreshed = false;
        ++m_generation;
    }

    for (auto pending = m_pending_queries.begin(); pending != m_pending_queries.end();) {
        if (pending->future.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
            auto completed_status = pending->future.get();
            if (pending->generation == m_generation && pending->directory == m_working_directory) {
                m_status = std::move(completed_status);
            }
            pending = m_pending_queries.erase(pending);
        } else {
            ++pending;
        }
    }

    if ((!m_has_refreshed || now - m_last_refresh >= kRefreshInterval) &&
        m_pending_queries.size() < kMaxConcurrentQueries) {
        if (m_pending_queries.capacity() < kMaxConcurrentQueries)
            m_pending_queries.reserve(kMaxConcurrentQueries);
        const auto query_directory = m_working_directory;
        const std::size_t query_generation = m_generation;
        try {
            auto query = std::async(std::launch::async,
                                    [query_directory] { return queryGitStatus(query_directory); });
            m_pending_queries.push_back({query_directory, query_generation, std::move(query)});
            m_last_refresh = now;
            m_has_refreshed = true;
        } catch (const std::system_error &) {
            m_last_refresh = now;
            m_has_refreshed = true;
        }
    }
    return m_status;
}

std::string GitStatusMonitor::label() const {
    return m_status ? m_status->label() : "Git unavailable";
}

std::string GitStatusMonitor::details() const {
    return m_status
               ? m_status->details()
               : "Git status unavailable (Git is missing, no repository was found, or the status "
                 "query timed out).";
}
