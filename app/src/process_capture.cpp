#include "process_capture.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <string>

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

namespace simulator::process_capture {

namespace {

#ifdef _WIN32
std::optional<std::wstring> toWide(std::string_view text) {
    if (text.empty())
        return std::wstring();
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return std::nullopt;
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                            length) != length)
        return std::nullopt;
    return wide;
}

std::wstring quoteArgument(const std::wstring &argument) {
    if (argument.empty())
        return L"\"\"";
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;

    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        quoted.push_back(character);
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

bool environmentNameRemoved(std::wstring_view name, const std::vector<std::string> &remove_names) {
    for (const std::string &removed : remove_names) {
        const auto wide = toWide(removed);
        if (!wide || wide->size() != name.size())
            continue;
        bool equal = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            wchar_t left = name[i];
            wchar_t right = (*wide)[i];
            if (left >= L'a' && left <= L'z')
                left -= L'a' - L'A';
            if (right >= L'a' && right <= L'z')
                right -= L'a' - L'A';
            if (left != right) {
                equal = false;
                break;
            }
        }
        if (equal)
            return true;
    }
    return false;
}

std::optional<std::vector<wchar_t>> environmentBlock(const std::vector<std::string> &remove_names) {
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
        if (!environmentNameRemoved(entry.substr(0, name_end), remove_names)) {
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
#else
bool environmentNameRemoved(std::string_view entry, const std::vector<std::string> &remove_names) {
    const std::size_t name_end = entry.find('=');
    if (name_end == std::string_view::npos)
        return false;
    const std::string_view name = entry.substr(0, name_end);
    return std::find(remove_names.begin(), remove_names.end(), name) != remove_names.end();
}
#endif

} // namespace

std::optional<std::filesystem::path> findExecutable(std::string_view program) {
#ifdef _WIN32
    const auto wide_program = toWide(program);
    if (!wide_program)
        return std::nullopt;
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
                const auto executable = path / (*wide_program + L".exe");
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
#else
    const char *path_env = std::getenv("PATH");
    const std::string search_path = path_env != nullptr ? path_env : "/bin:/usr/bin";
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = search_path.find(':', begin);
        const std::size_t length = end == std::string::npos ? std::string::npos : end - begin;
        const std::string directory = search_path.substr(begin, length);
        const std::filesystem::path executable = directory.empty()
                                                     ? std::filesystem::path(program)
                                                     : std::filesystem::path(directory) / program;
        // Relative PATH entries would resolve against the process directory.
        if (executable.is_absolute()) {
            std::error_code error;
            if (std::filesystem::is_regular_file(executable, error) && !error)
                return executable.lexically_normal();
        }
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return std::nullopt;
#endif
}

std::optional<Result> run(std::string_view program, const std::vector<std::string> &arguments,
                          const Options &options) {
    const auto executable = findExecutable(program);
    if (!executable)
        return std::nullopt;

    Result result;
    result.started = true;

#ifdef _WIN32
    auto environment = environmentBlock(options.remove_environment_names);
    if (!environment)
        return std::nullopt;

    std::wstring command_line;
    try {
        command_line = quoteArgument(executable->wstring());
    } catch (const std::filesystem::filesystem_error &) {
        result.started = false;
        return result;
    }
    for (const std::string &argument : arguments) {
        const auto wide = toWide(argument);
        if (!wide) {
            result.started = false;
            return result;
        }
        command_line += L' ';
        command_line += quoteArgument(*wide);
    }

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security_attributes, 0)) {
        result.started = false;
        return result;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        result.started = false;
        return result;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        result.started = false;
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
    job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &job_limits,
                                 sizeof(job_limits))) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        CloseHandle(job);
        result.started = false;
        return result;
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = write_pipe;
    startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::wstring executable_path;
    std::wstring directory;
    try {
        executable_path = executable->wstring();
        if (!options.working_directory.empty())
            directory = options.working_directory.wstring();
    } catch (const std::filesystem::filesystem_error &) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        CloseHandle(job);
        result.started = false;
        return result;
    }

    const BOOL created = CreateProcessW(
        executable_path.c_str(), command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment->data(),
        directory.empty() ? nullptr : directory.c_str(), &startup_info, &process_info);
    CloseHandle(write_pipe);
    if (!created) {
        CloseHandle(read_pipe);
        CloseHandle(job);
        result.started = false;
        return result;
    }
    if (!AssignProcessToJobObject(job, process_info.hProcess)) {
        TerminateProcess(process_info.hProcess, 124);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(read_pipe);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
        result.started = false;
        return result;
    }
    if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
        TerminateJobObject(job, 124);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(read_pipe);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
        result.started = false;
        return result;
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

    result.output.reserve(1024);
    bool read_ok = true;
    bool pipe_closed = false;
    bool process_done = false;
    std::array<char, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
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
                if (result.output.size() + bytes_read > options.max_output_bytes) {
                    result.output_truncated = true;
                } else {
                    result.output.append(buffer.data(), bytes_read);
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
            return result;
        }
        if (pipe_closed && process_done)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            terminate_job();
            return result;
        }
        Sleep(10);
    }
    CloseHandle(read_pipe);

    DWORD exit_code = 1;
    if (GetExitCodeProcess(process_info.hProcess, &exit_code))
        result.exit_code = static_cast<int>(exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    CloseHandle(job);
    return result;
#else
    int pipe_fds[2]{};
    if (pipe(pipe_fds) != 0) {
        result.started = false;
        return result;
    }
    for (int &pipe_fd : pipe_fds) {
        if (pipe_fd <= STDERR_FILENO) {
            const int safe_fd = fcntl(pipe_fd, F_DUPFD, STDERR_FILENO + 1);
            if (safe_fd < 0) {
                close(pipe_fds[0]);
                close(pipe_fds[1]);
                result.started = false;
                return result;
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
        result.started = false;
        return result;
    }

    // Prepare all argv/env/path storage before fork: the child of this
    // multithreaded process uses only async-signal-safe calls before execve.
    std::vector<std::string> args{std::string(program)};
    args.insert(args.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::vector<char *> environment;
    for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (!environmentNameRemoved(*entry, options.remove_environment_names))
            environment.push_back(*entry);
    }
    environment.push_back(nullptr);

    std::string executable_path = executable->string();
    std::string working_directory;
    if (!options.working_directory.empty())
        working_directory = options.working_directory.string();

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        result.started = false;
        return result;
    }
    if (pid == 0) {
        setpgid(0, 0);
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(pipe_fds[1]);
        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0)
            _exit(127);
        execve(executable_path.c_str(), argv.data(), environment.data());
        _exit(127);
    }

    (void)setpgid(pid, pid);
    close(pipe_fds[1]);
    result.output.reserve(1024);
    bool read_ok = true;
    bool pipe_closed = false;
    bool child_done = false;
    int child_status = 0;
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;

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
                if (result.output.size() + count > options.max_output_bytes)
                    result.output_truncated = true;
                else
                    result.output.append(buffer.data(), count);
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
            return result;
        }
        if (pipe_closed && child_done)
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            stop_child();
            return result;
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

    if (WIFEXITED(child_status))
        result.exit_code = WEXITSTATUS(child_status);
    return result;
#endif
}

} // namespace simulator::process_capture
