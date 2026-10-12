#include "git_status.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace {

constexpr std::size_t kMaxStatusOutputBytes = 4 * 1024 * 1024;
constexpr auto kGitStatusProcessTimeout = std::chrono::seconds(1);

#ifdef _WIN32
std::optional<std::filesystem::path> findGitExecutable() {
    const DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (required == 0)
        return std::nullopt;
    std::wstring search_path(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"PATH", search_path.data(), required);
    if (length == 0 || length >= required)
        return std::nullopt;
    search_path.resize(length);

    std::size_t begin = 0;
    while (begin <= search_path.size()) {
        const std::size_t end = search_path.find(L';', begin);
        std::wstring directory = search_path.substr(
            begin, end == std::wstring::npos ? search_path.size() - begin : end - begin);
        if (directory.size() >= 2 && directory.front() == L'"' && directory.back() == L'"')
            directory = directory.substr(1, directory.size() - 2);
        if (!directory.empty()) {
            const std::filesystem::path path(directory);
            if (path.is_absolute()) {
                const auto executable = path / L"git.exe";
                std::error_code error;
                if (std::filesystem::is_regular_file(executable, error) && !error)
                    return executable;
            }
        }
        if (end == std::wstring::npos)
            break;
        begin = end + 1;
    }
    return std::nullopt;
}

bool isGitRepositoryOverride(std::wstring_view name) {
    constexpr std::array<std::wstring_view, 4> overrides = {L"GIT_DIR", L"GIT_WORK_TREE",
                                                            L"GIT_COMMON_DIR", L"GIT_INDEX_FILE"};
    return std::any_of(overrides.begin(), overrides.end(), [name](std::wstring_view override) {
        if (name.size() != override.size())
            return false;
        for (std::size_t i = 0; i < name.size(); ++i) {
            wchar_t character = name[i];
            if (character >= L'a' && character <= L'z')
                character -= L'a' - L'A';
            if (character != override[i])
                return false;
        }
        return true;
    });
}

std::optional<std::vector<wchar_t>> gitEnvironmentBlock() {
    wchar_t *environment_block = GetEnvironmentStringsW();
    if (environment_block == nullptr)
        return std::nullopt;
    wchar_t *current = environment_block;

    std::vector<wchar_t> environment;
    while (*current != L'\0') {
        wchar_t *end = current;
        while (*end != L'\0')
            ++end;
        const std::wstring_view entry(current, static_cast<std::size_t>(end - current));
        const std::size_t name_end = entry.find(L'=', entry.starts_with(L'=') ? 1 : 0);
        if (!isGitRepositoryOverride(entry.substr(0, name_end))) {
            environment.insert(environment.end(), current, end);
            environment.push_back(L'\0');
        }
        current = end + 1;
    }
    FreeEnvironmentStringsW(environment_block);
    if (environment.empty())
        environment.push_back(L'\0');
    environment.push_back(L'\0');
    return environment;
}

std::optional<std::string> runGitStatus(const std::filesystem::path &working_directory) {
    const auto executable = findGitExecutable();
    if (!executable)
        return std::nullopt;
    auto environment = gitEnvironmentBlock();
    if (!environment)
        return std::nullopt;

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security_attributes, 0))
        return std::nullopt;
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return std::nullopt;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return std::nullopt;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
    job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &job_limits,
                                 sizeof(job_limits))) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        CloseHandle(job);
        return std::nullopt;
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = write_pipe;
    startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::wstring command_line;
    std::wstring executable_path;
    std::wstring directory;
    try {
        executable_path = executable->wstring();
        directory = working_directory.wstring();
        command_line = L"\"" + executable_path +
                       L"\" -c core.fsmonitor=false --no-optional-locks status "
                       L"--porcelain=v2 --branch --untracked-files=normal";
    } catch (const std::filesystem::filesystem_error &) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        CloseHandle(job);
        return std::nullopt;
    }

    const BOOL created =
        CreateProcessW(executable_path.c_str(), command_line.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                       environment->data(), directory.c_str(), &startup_info, &process_info);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        CloseHandle(job);
        return std::nullopt;
    }
    if (!AssignProcessToJobObject(job, process_info.hProcess)) {
        TerminateProcess(process_info.hProcess, 124);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(read_pipe);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
        return std::nullopt;
    }
    if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
        TerminateJobObject(job, 124);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(read_pipe);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
        return std::nullopt;
    }

    const auto terminate_job = [&]() {
        if (!TerminateJobObject(job, 124))
            (void)TerminateProcess(process_info.hProcess, 124);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(read_pipe);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
    };

    std::string output;
    output.reserve(1024);
    bool oversized = false;
    bool read_ok = true;
    bool pipe_closed = false;
    bool process_done = false;
    std::array<char, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + kGitStatusProcessTimeout;
    while (!pipe_closed || !process_done) {
        if (!pipe_closed) {
            DWORD available = 0;
            if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE)
                    pipe_closed = true;
                else
                    read_ok = false;
            }
            while (read_ok && !pipe_closed && available > 0) {
                const DWORD read_size =
                    std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
                DWORD bytes_read = 0;
                if (!ReadFile(read_pipe, buffer.data(), read_size, &bytes_read, nullptr)) {
                    if (GetLastError() == ERROR_BROKEN_PIPE)
                        pipe_closed = true;
                    else
                        read_ok = false;
                    break;
                }
                if (bytes_read == 0) {
                    pipe_closed = true;
                    break;
                }
                if (output.size() + bytes_read > kMaxStatusOutputBytes) {
                    oversized = true;
                } else {
                    output.append(buffer.data(), bytes_read);
                }
                if (!PeekNamedPipe(read_pipe, nullptr, 0, nullptr, &available, nullptr)) {
                    if (GetLastError() == ERROR_BROKEN_PIPE)
                        pipe_closed = true;
                    else
                        read_ok = false;
                    break;
                }
            }
        }

        const DWORD wait_result = WaitForSingleObject(process_info.hProcess, 0);
        if (wait_result == WAIT_OBJECT_0)
            process_done = true;
        else if (wait_result != WAIT_TIMEOUT)
            read_ok = false;

        if (!read_ok) {
            terminate_job();
            return std::nullopt;
        }
        if (pipe_closed && process_done)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            terminate_job();
            return std::nullopt;
        }
        Sleep(10);
    }
    CloseHandle(read_pipe);

    DWORD exit_code = 1;
    const bool got_exit_code = GetExitCodeProcess(process_info.hProcess, &exit_code) != FALSE;
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    CloseHandle(job);
    if (oversized || !got_exit_code || exit_code != 0)
        return std::nullopt;
    return output;
}
#else
std::vector<std::string> gitExecutableCandidates() {
    const char *path_env = std::getenv("PATH");
    const std::string search_path = path_env != nullptr ? path_env : "/bin:/usr/bin";
    std::vector<std::string> candidates;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = search_path.find(':', begin);
        const std::size_t length = end == std::string::npos ? std::string::npos : end - begin;
        const std::string directory = search_path.substr(begin, length);
        const std::filesystem::path executable = directory.empty()
                                                     ? std::filesystem::path("git")
                                                     : std::filesystem::path(directory) / "git";
        // Relative PATH entries would resolve against the project directory after chdir().
        // Ignore them rather than running a project-controlled executable.
        if (executable.is_absolute())
            candidates.push_back(executable.lexically_normal().string());
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return candidates;
}

bool isGitRepositoryOverride(const char *entry) {
    constexpr std::array<std::string_view, 4> overrides = {"GIT_DIR", "GIT_WORK_TREE",
                                                           "GIT_COMMON_DIR", "GIT_INDEX_FILE"};
    const std::string_view environment_entry(entry);
    const std::size_t name_end = environment_entry.find('=');
    return name_end != std::string_view::npos &&
           std::find(overrides.begin(), overrides.end(), environment_entry.substr(0, name_end)) !=
               overrides.end();
}

std::optional<std::string> runGitStatus(const std::filesystem::path &working_directory) {
    int pipe_fds[2]{};
    if (pipe(pipe_fds) != 0)
        return std::nullopt;
    for (int &pipe_fd : pipe_fds) {
        if (pipe_fd <= STDERR_FILENO) {
            const int safe_fd = fcntl(pipe_fd, F_DUPFD, STDERR_FILENO + 1);
            if (safe_fd < 0) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                return std::nullopt;
            }
            close(pipe_fd);
            pipe_fd = safe_fd;
        }
    }
    const int read_flags = fcntl(pipe_fds[0], F_GETFL);
    if (read_flags < 0 || fcntl(pipe_fds[0], F_SETFL, read_flags | O_NONBLOCK) != 0 ||
        fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return std::nullopt;
    }

    // Prepare all argv/env/path storage before fork: the child of this multithreaded
    // GUI process uses only async-signal-safe calls before execve.
    std::vector<std::string> args{"git",
                                  "-c",
                                  "core.fsmonitor=false",
                                  "--no-optional-locks",
                                  "status",
                                  "--porcelain=v2",
                                  "--branch",
                                  "--untracked-files=normal"};
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    auto path_storage = gitExecutableCandidates();
    std::vector<char *> candidates;
    candidates.reserve(path_storage.size());
    for (auto &candidate : path_storage)
        candidates.push_back(candidate.data());
    if (candidates.empty()) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return std::nullopt;
    }

    std::vector<char *> environment;
    for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (!isGitRepositoryOverride(*entry))
            environment.push_back(*entry);
    }
    environment.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return std::nullopt;
    }
    if (pid == 0) {
        setpgid(0, 0);
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(pipe_fds[1]);
        if (chdir(working_directory.c_str()) != 0)
            _exit(127);

        for (char *candidate : candidates) {
            execve(candidate, argv.data(), environment.data());
            if (errno != EACCES && errno != ENOENT && errno != ENOTDIR)
                break;
        }
        _exit(127);
    }

    (void)setpgid(pid, pid);
    close(pipe_fds[1]);
    std::string output;
    output.reserve(1024);
    bool oversized = false;
    bool read_ok = true;
    bool pipe_closed = false;
    bool child_done = false;
    int child_status = 0;
    const auto deadline = std::chrono::steady_clock::now() + kGitStatusProcessTimeout;

    const auto stop_child = [pid, pipe_fd = pipe_fds[0], &child_done]() {
        if (kill(-pid, SIGKILL) != 0)
            (void)kill(pid, SIGKILL);
        if (!child_done) {
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
            }
            child_done = true;
        }
        close(pipe_fd);
    };

    std::array<char, 4096> buffer{};
    while (!pipe_closed || !child_done) {
        if (!pipe_closed) {
            while (true) {
                const ssize_t bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
                if (bytes_read == 0) {
                    pipe_closed = true;
                    break;
                }
                if (bytes_read < 0) {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    read_ok = false;
                    pipe_closed = true;
                    break;
                }
                const auto count = static_cast<std::size_t>(bytes_read);
                if (output.size() + count > kMaxStatusOutputBytes)
                    oversized = true;
                else
                    output.append(buffer.data(), count);
            }
        }

        if (!child_done) {
            const pid_t waited = waitpid(pid, &child_status, WNOHANG);
            if (waited == pid)
                child_done = true;
            else if (waited < 0 && errno != EINTR)
                read_ok = false;
        }
        if (!read_ok) {
            stop_child();
            return std::nullopt;
        }
        if (pipe_closed && child_done)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            stop_child();
            return std::nullopt;
        }

        if (pipe_closed) {
            (void)poll(nullptr, 0, 20);
        } else {
            pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
            if (poll(&descriptor, 1, 20) < 0 && errno != EINTR)
                read_ok = false;
        }
    }
    close(pipe_fds[0]);

    if (oversized || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return std::nullopt;
    return output;
}
#endif

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

std::optional<GitStatus> queryGitStatus(const std::filesystem::path &working_directory) {
    if (working_directory.empty())
        return std::nullopt;
    std::error_code error;
    const std::filesystem::path absolute_directory =
        std::filesystem::absolute(working_directory, error).lexically_normal();
    if (error || !std::filesystem::is_directory(absolute_directory, error) || error)
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
                                         float available_width, float fps_width, float item_spacing,
                                         const std::function<float(std::string_view)> &measure) {
    constexpr std::string_view unavailable_label = "Git unavailable";
    const float minimum_git_width =
        status ? measure(gitStatusHeaderSummary(*status)) : measure(unavailable_label);
    const float project_gap_count = project_title.empty() ? 1.0f : 2.0f;
    const float project_width_budget = std::max(
        0.0f, available_width - fps_width - item_spacing * project_gap_count - minimum_git_width);

    GitStatusHeaderLabels labels;
    labels.project_title = fitTextToWidth(project_title, project_width_budget, measure);
    const bool show_project_title = !labels.project_title.empty();
    const float gap_count = show_project_title ? 2.0f : 1.0f;
    const float git_width =
        std::max(0.0f, available_width - fps_width - item_spacing * gap_count -
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
