#include "external_tool_runner.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef _WIN32
// Read by buildExecPlan(): the environment pointer array as it stands at fork time.
// Its strings stay shared with the parent's environment, not copied.
extern char **environ;
#endif

namespace fs = std::filesystem;
namespace {

using json = nlohmann::json;
constexpr auto kProcessTimeout = std::chrono::seconds(30);

struct ProcessResult {
    bool launched = false;
    int exit_code = -1;
    std::string error;
};

fs::path makeWorkDir(const fs::path &requested) {
    if (!requested.empty())
        return requested;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("rfsim_external_tool_" + std::to_string(stamp));
}

fs::path createUniqueWorkDir(const fs::path &base, std::error_code &ec) {
    // Never reuse a caller-designated workspace: each invocation gets its own
    // directory so concurrent or repeated runs cannot clobber each other's
    // request/result files.
    static std::atomic<std::uint64_t> sequence{0};
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path candidate =
            base / ("run-" + std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
        if (fs::create_directories(candidate, ec))
            return candidate;
        if (ec)
            return {};
    }
    ec = std::make_error_code(std::errc::file_exists);
    return {};
}

std::string manifestKindToString(const ExtensionManifest &manifest) {
    return manifest.kind == ExtensionKind::ExternalTool ? "external-tool" : "data-pack";
}

json buildRequestJson(const ExtensionManifest &manifest, const ExternalToolRequest &request,
                      const fs::path &request_path, const fs::path &result_path) {
    return json{{"contract_version", request.contract_version},
                {"action_label", request.action_label},
                {"project_root", request.project_root.generic_string()},
                {"selected_path", request.selected_path.generic_string()},
                {"work_dir", request_path.parent_path().generic_string()},
                {"request_path", request_path.generic_string()},
                {"result_path", result_path.generic_string()},
                {"manifest",
                 {{"id", manifest.id},
                  {"name", manifest.name},
                  {"version", manifest.version},
                  {"kind", manifestKindToString(manifest)},
                  {"entry_path", manifest.entry_path.generic_string()},
                  {"root_dir", manifest.root_dir.generic_string()}}}};
}

bool writeJsonFile(const fs::path &path, const json &value, std::string &error) {
    std::error_code ec;
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "could not create output directory";
        return false;
    }

    std::ofstream out(path);
    if (!out) {
        error = "could not open output file";
        return false;
    }

    out << value.dump(2) << '\n';
    if (!out.good()) {
        error = "could not write output file";
        return false;
    }

    return true;
}

#ifdef _WIN32
std::wstring quoteWindowsArg(const std::wstring &arg) {
    if (arg.empty())
        return L"\"\"";

    const bool needs_quotes = arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needs_quotes)
        return arg;

    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }

        if (ch == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
            continue;
        }

        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
    }

    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring buildCommandLine(const std::vector<fs::path> &argv) {
    std::wstring command_line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0)
            command_line.push_back(L' ');
        command_line += quoteWindowsArg(argv[i].wstring());
    }
    return command_line;
}

ProcessResult launchProcess(const std::vector<fs::path> &argv, const fs::path &working_dir) {
    if (argv.empty())
        return {.launched = false, .exit_code = -1, .error = "empty argv"};

    std::wstring command_line = buildCommandLine(argv);
    std::wstring working_dir_w;
    if (!working_dir.empty())
        working_dir_w = working_dir.wstring();

    // Keep the tool inside a job object so a timeout can terminate the whole
    // descendant tree, not just the direct child.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr)
        return {.launched = false, .exit_code = -1, .error = "CreateJobObjectW failed"};

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    // Start suspended so the process can be placed in the job before it (or
    // anything it spawns) can run.
    const BOOL created = CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
        working_dir.empty() ? nullptr : working_dir_w.c_str(), &startup_info, &process_info);
    if (!created) {
        CloseHandle(job);
        return {.launched = false, .exit_code = -1, .error = "CreateProcessW failed"};
    }

    // Refuse to run without descendant-kill control: if the child cannot be
    // placed in the job, terminate it while still suspended and report launch
    // failure instead of executing with only direct-child termination.
    if (!AssignProcessToJobObject(job, process_info.hProcess)) {
        TerminateProcess(process_info.hProcess, 124);
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
        return {.launched = false, .exit_code = -1, .error = "could not assign process to job"};
    }

    ResumeThread(process_info.hThread);

    const auto deadline = std::chrono::steady_clock::now() + kProcessTimeout;
    while (true) {
        const DWORD wait = WaitForSingleObject(process_info.hProcess, 100);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait != WAIT_TIMEOUT) {
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
            CloseHandle(job);
            return {.launched = true, .exit_code = -1, .error = "WaitForSingleObject failed"};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            TerminateJobObject(job, 124);
            WaitForSingleObject(process_info.hProcess, INFINITE);
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
            CloseHandle(job);
            return {.launched = true, .exit_code = 124, .error = "process timed out"};
        }
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        CloseHandle(job);
        return {.launched = true, .exit_code = -1, .error = "GetExitCodeProcess failed"};
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    CloseHandle(job);
    return {.launched = true, .exit_code = static_cast<int>(exit_code), .error = {}};
}
#else
// Everything the forked child needs, prepared in the parent before fork(). In a
// multithreaded process the child may use only async-signal-safe calls, so it must
// not allocate, lock, or search PATH. Its one program-level write is the
// preallocated shell_argv[1] slot, set before the /bin/sh fallback.
struct PosixExecPlan {
    std::vector<std::string> args;  // argv text; owns the bytes argv points into
    std::vector<std::string> paths; // execve() candidates in PATH search order
    std::vector<char *> argv;       // nullptr-terminated
    std::vector<char *> candidates; // one pointer per entry of paths
    std::vector<char *> shell_argv; // {"/bin/sh", <script slot>, args[1..], nullptr}
    std::vector<char *> envp;       // nullptr-terminated snapshot of the environment
};

// Lists the execve() candidates for a bare command name, in the order execvp()
// searches PATH. A name containing '/' is used as given. An empty PATH entry means
// the working directory, and an unset PATH means the conventional default. Relative
// candidates resolve in the child, after its chdir().
std::vector<std::string> executableCandidates(const std::string &file) {
    if (file.find('/') != std::string::npos)
        return {file};

    const char *path_env = std::getenv("PATH");
    const std::string search_path = path_env != nullptr ? path_env : "/bin:/usr/bin";
    std::vector<std::string> candidates;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = search_path.find(':', begin);
        const std::size_t length = end == std::string::npos ? std::string::npos : end - begin;
        const std::string dir = search_path.substr(begin, length);
        candidates.push_back(dir.empty() ? file : dir + "/" + file);
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return candidates;
}

PosixExecPlan buildExecPlan(const std::vector<fs::path> &argv) {
    PosixExecPlan plan;
    plan.args.reserve(argv.size());
    for (const auto &arg : argv)
        plan.args.push_back(arg.string());
    plan.paths = executableCandidates(plan.args.front());

    // Pointers are taken only after every string they reference exists.
    for (auto &arg : plan.args)
        plan.argv.push_back(arg.data());
    plan.argv.push_back(nullptr);
    for (auto &path : plan.paths)
        plan.candidates.push_back(path.data());

    plan.shell_argv.push_back(const_cast<char *>("/bin/sh"));
    plan.shell_argv.push_back(nullptr); // script slot, filled per candidate in the child
    for (std::size_t i = 1; i < plan.args.size(); ++i)
        plan.shell_argv.push_back(plan.args[i].data());
    plan.shell_argv.push_back(nullptr);

    for (char **entry = environ; *entry != nullptr; ++entry)
        plan.envp.push_back(*entry);
    plan.envp.push_back(nullptr);
    return plan;
}

// Runs in the forked child, where only async-signal-safe calls are allowed. Continues
// past EACCES, ENOENT, and ENOTDIR, as execvp() does. Other errno values stop the
// search, so this is not an exact execvp() replica. Returns only when no candidate
// could be executed.
void execPlanInChild(PosixExecPlan &plan) {
    for (char *candidate : plan.candidates) {
        execve(candidate, plan.argv.data(), plan.envp.data());
        if (errno == ENOEXEC) {
            // A file the kernel will not run is handed to the shell, as execvp() does.
            plan.shell_argv[1] = candidate;
            execve("/bin/sh", plan.shell_argv.data(), plan.envp.data());
        }
        if (errno != EACCES && errno != ENOENT && errno != ENOTDIR)
            return;
    }
}

ProcessResult launchProcess(const std::vector<fs::path> &argv, const fs::path &working_dir) {
    if (argv.empty())
        return {.launched = false, .exit_code = -1, .error = "empty argv"};

    // Built before fork(), so the child below allocates nothing and searches no PATH.
    PosixExecPlan plan = buildExecPlan(argv);

    const pid_t pid = fork();
    if (pid < 0)
        return {.launched = false, .exit_code = -1, .error = "fork failed"};

    if (pid == 0) {
        // Own process group, so a timeout signals the tool and descendants still in its group.
        setpgid(0, 0);

        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0)
            _exit(127);

        execPlanInChild(plan);
        _exit(127);
    }

    // Parent-side group creation closes the window before the child's own
    // setpgid(0, 0) runs; whichever lands first wins, so the group exists by
    // the time a timeout kill is needed.
    setpgid(pid, pid);

    const auto deadline = std::chrono::steady_clock::now() + kProcessTimeout;
    int status = 0;
    while (true) {
        const pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid)
            break;
        if (wait_result < 0)
            return {.launched = true, .exit_code = -1, .error = "waitpid failed"};
        if (wait_result == 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // Negative pid = whole process group (tool + descendants).
                kill(-pid, SIGKILL);
                waitpid(pid, &status, 0);
                return {.launched = true, .exit_code = 124, .error = "process timed out"};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (WIFEXITED(status))
        return {.launched = true, .exit_code = WEXITSTATUS(status), .error = {}};
    if (WIFSIGNALED(status))
        return {.launched = true,
                .exit_code = 128 + WTERMSIG(status),
                .error = "process terminated by signal"};

    return {.launched = true, .exit_code = -1, .error = "process did not exit cleanly"};
}
#endif

std::vector<fs::path> buildCommand(const ExtensionManifest &manifest, const fs::path &request_path,
                                   const fs::path &result_path) {
    std::vector<fs::path> argv;
#ifdef _WIN32
    const bool is_python_script = manifest.entry_path.extension() == ".py";
    if (is_python_script) {
        argv.emplace_back("python");
        argv.push_back(manifest.entry_path);
    } else {
        argv.push_back(manifest.entry_path);
    }
#else
    const bool is_python_script = manifest.entry_path.extension() == ".py";
    if (is_python_script) {
        argv.emplace_back("python3");
        argv.push_back(manifest.entry_path);
    } else {
        argv.push_back(manifest.entry_path);
    }
#endif
    argv.emplace_back("--request");
    argv.push_back(request_path);
    argv.emplace_back("--result");
    argv.push_back(result_path);
    return argv;
}

std::string readJsonMessage(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        return {};

    const json value = json::parse(in, nullptr, false);
    if (value.is_discarded() || !value.is_object())
        return {};

    if (value.contains("message") && value["message"].is_string())
        return value["message"].get<std::string>();
    return "ok";
}

} // namespace

ExternalToolRunResult ExternalToolRunner::run(const ExtensionManifest &manifest,
                                              const ExternalToolRequest &request) const {
    ExternalToolRunResult result;
    result.work_dir = makeWorkDir(request.work_dir);

    if (manifest.kind != ExtensionKind::ExternalTool) {
        result.message = "manifest is not an external tool";
        return result;
    }
    if (manifest.entry_path.empty()) {
        result.message = "entry path is empty";
        return result;
    }
    if (!fs::exists(manifest.entry_path)) {
        result.message = "entry path does not exist";
        return result;
    }

    std::error_code ec;
    fs::create_directories(result.work_dir, ec);
    if (ec) {
        result.message = "could not create work directory";
        return result;
    }

    // Isolate each invocation in its own fresh workspace so repeated or
    // concurrent runs never share (and clobber) request/result files.
    result.work_dir = createUniqueWorkDir(result.work_dir, ec);
    if (ec) {
        result.message = "could not create work directory";
        return result;
    }

    const fs::path request_path = result.work_dir / "request.json";
    const fs::path result_path = result.work_dir / "result.json";
    result.result_path = result_path;

    const json request_json = buildRequestJson(manifest, request, request_path, result_path);
    std::string error;
    if (!writeJsonFile(request_path, request_json, error)) {
        result.message = error;
        return result;
    }

    const std::vector<fs::path> argv = buildCommand(manifest, request_path, result_path);
    const ProcessResult process = launchProcess(argv, result.work_dir);
    result.exit_code = process.exit_code;
    if (!process.launched) {
        result.message = process.error;
        return result;
    }
    if (process.exit_code != 0) {
        std::ostringstream oss;
        oss << "external tool exited with code " << process.exit_code;
        result.message = oss.str();
        return result;
    }

    if (!fs::exists(result_path)) {
        result.message = "result file missing";
        return result;
    }

    // Bound the result file before parsing: a runaway tool must not be able
    // to force unbounded JSON parsing of a huge output.
    const std::uintmax_t size_bytes = fs::file_size(result_path, ec);
    if (ec) {
        result.message = "could not read result file";
        return result;
    }
    if (size_bytes > maxResultFileBytes) {
        result.message = "result file too large";
        return result;
    }

    const std::string message = readJsonMessage(result_path);
    if (message.empty()) {
        result.message = "result file is not valid JSON";
        return result;
    }

    result.ok = true;
    result.message = message;
    return result;
}
