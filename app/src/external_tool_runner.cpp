#include "external_tool_runner.h"

#include <chrono>
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
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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

std::string manifestKindToString(const ExtensionManifest &manifest) {
    return manifest.kind == ExtensionKind::ExternalTool ? "external-tool" : "data-pack";
}

json buildRequestJson(const ExtensionManifest &manifest, const ExternalToolRequest &request,
                      const fs::path &request_path) {
    return json{{"contract_version", request.contract_version},
                {"action_label", request.action_label},
                {"project_root", request.project_root.generic_string()},
                {"selected_path", request.selected_path.generic_string()},
                {"work_dir", request_path.parent_path().generic_string()},
                {"request_path", request_path.generic_string()},
                {"result_path", request.result_path.generic_string()},
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

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    const BOOL created = CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
        working_dir.empty() ? nullptr : working_dir_w.c_str(), &startup_info, &process_info);
    if (!created)
        return {.launched = false, .exit_code = -1, .error = "CreateProcessW failed"};

    const auto deadline = std::chrono::steady_clock::now() + kProcessTimeout;
    while (true) {
        const DWORD wait = WaitForSingleObject(process_info.hProcess, 100);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait != WAIT_TIMEOUT) {
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
            return {.launched = true, .exit_code = -1, .error = "WaitForSingleObject failed"};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            TerminateProcess(process_info.hProcess, 124);
            WaitForSingleObject(process_info.hProcess, INFINITE);
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
            return {.launched = true, .exit_code = 124, .error = "process timed out"};
        }
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return {.launched = true, .exit_code = -1, .error = "GetExitCodeProcess failed"};
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return {.launched = true, .exit_code = static_cast<int>(exit_code), .error = {}};
}
#else
ProcessResult launchProcess(const std::vector<fs::path> &argv, const fs::path &working_dir) {
    if (argv.empty())
        return {.launched = false, .exit_code = -1, .error = "empty argv"};

    const pid_t pid = fork();
    if (pid < 0)
        return {.launched = false, .exit_code = -1, .error = "fork failed"};

    if (pid == 0) {
        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0)
            _exit(127);

        std::vector<std::string> storage;
        storage.reserve(argv.size());
        for (const auto &arg : argv)
            storage.push_back(arg.string());

        std::vector<char *> cargv;
        cargv.reserve(storage.size() + 1);
        for (auto &arg : storage)
            cargv.push_back(arg.data());
        cargv.push_back(nullptr);

        execvp(cargv.front(), cargv.data());
        _exit(127);
    }

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
                kill(pid, SIGKILL);
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

std::vector<fs::path> buildCommand(const ExtensionManifest &manifest,
                                   const ExternalToolRequest &request,
                                   const fs::path &request_path) {
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
    argv.push_back(request.result_path);
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
    result.result_path = request.result_path;
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

    if (!request.result_path.parent_path().empty()) {
        fs::create_directories(request.result_path.parent_path(), ec);
        if (ec) {
            result.message = "could not create result directory";
            return result;
        }
    }

    const fs::path request_path = result.work_dir / "request.json";
    const json request_json = buildRequestJson(manifest, request, request_path);
    std::string error;
    if (!writeJsonFile(request_path, request_json, error)) {
        result.message = error;
        return result;
    }

    const std::vector<fs::path> argv = buildCommand(manifest, request, request_path);
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

    if (!fs::exists(request.result_path)) {
        result.message = "result file missing";
        return result;
    }

    const std::string message = readJsonMessage(request.result_path);
    if (message.empty()) {
        result.message = "result file is not valid JSON";
        return result;
    }

    result.ok = true;
    result.message = message;
    return result;
}
