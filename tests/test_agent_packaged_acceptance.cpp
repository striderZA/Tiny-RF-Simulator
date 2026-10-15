#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "agent_api_test_support.h"
#include "agent_server.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Json = nlohmann::json;

#ifndef RFSIM_MCP_EXECUTABLE
#error "RFSIM_MCP_EXECUTABLE must name the built rf-sim-mcp executable"
#endif

namespace {

class ScratchDirectory {
  public:
    ScratchDirectory() {
        static std::atomic<unsigned> sequence{0};
        path =
            fs::temp_directory_path() / ("rfsim-agent-packaged-" + test_temp_paths::processTag() +
                                         "-" + std::to_string(sequence.fetch_add(1)));
        fs::create_directories(path);
    }
    ~ScratchDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    fs::path path;
};

class McpProcess {
  public:
    explicit McpProcess(const fs::path &endpoint) {
#ifdef _WIN32
        HANDLE child_stdin_read = nullptr;
        HANDLE parent_stdin_write = nullptr;
        HANDLE parent_stdout_read = nullptr;
        HANDLE child_stdout_write = nullptr;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        if (!CreatePipe(&child_stdin_read, &parent_stdin_write, &security, 0) ||
            !CreatePipe(&parent_stdout_read, &child_stdout_write, &security, 0))
            throw std::runtime_error("CreatePipe failed");
        SetHandleInformation(parent_stdin_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(parent_stdout_read, HANDLE_FLAG_INHERIT, 0);

        const fs::path executable(RFSIM_MCP_EXECUTABLE);
        std::wstring command =
            L"\"" + executable.wstring() + L"\" --endpoint \"" + endpoint.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = child_stdin_read;
        startup.hStdOutput = child_stdout_write;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};
        const BOOL started =
            CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdout_write);
        if (!started) {
            CloseHandle(parent_stdin_write);
            CloseHandle(parent_stdout_read);
            throw std::runtime_error("CreateProcessW failed to start rf-sim-mcp");
        }
        CloseHandle(process.hThread);
        m_process = process.hProcess;
        m_stdin = parent_stdin_write;
        m_stdout = parent_stdout_read;
#else
        int input[2];
        int output[2];
        if (pipe(input) != 0 || pipe(output) != 0)
            throw std::runtime_error("pipe failed");
        m_pid = fork();
        if (m_pid < 0)
            throw std::runtime_error("fork failed");
        if (m_pid == 0) {
            (void)dup2(input[0], STDIN_FILENO);
            (void)dup2(output[1], STDOUT_FILENO);
            close(input[0]);
            close(input[1]);
            close(output[0]);
            close(output[1]);
            const std::string exe = fs::path(RFSIM_MCP_EXECUTABLE).string();
            const std::string endpoint_string = endpoint.string();
            execl(exe.c_str(), exe.c_str(), "--endpoint", endpoint_string.c_str(),
                  static_cast<char *>(nullptr));
            _exit(127);
        }
        close(input[0]);
        close(output[1]);
        m_stdin = input[1];
        m_stdout = output[0];
#endif
    }

    ~McpProcess() { stop(); }
    McpProcess(const McpProcess &) = delete;
    McpProcess &operator=(const McpProcess &) = delete;

    bool send(const Json &message) {
        const std::string bytes = message.dump() + '\n';
        std::size_t sent = 0;
        while (sent < bytes.size()) {
#ifdef _WIN32
            DWORD count = 0;
            if (!WriteFile(m_stdin, bytes.data() + sent, static_cast<DWORD>(bytes.size() - sent),
                           &count, nullptr) ||
                count == 0)
                return false;
            sent += count;
#else
            const ssize_t count = write(m_stdin, bytes.data() + sent, bytes.size() - sent);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                return false;
            sent += static_cast<std::size_t>(count);
#endif
        }
        return true;
    }

    template <typename Pump>
    std::optional<Json> receive(Pump pump, std::chrono::seconds timeout = 5s) {
        std::string line;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            pump();
            char byte = 0;
#ifdef _WIN32
            DWORD available = 0;
            if (!PeekNamedPipe(m_stdout, nullptr, 0, nullptr, &available, nullptr))
                return std::nullopt;
            if (available == 0) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            DWORD count = 0;
            if (!ReadFile(m_stdout, &byte, 1, &count, nullptr) || count == 0)
                return std::nullopt;
#else
            pollfd descriptor{m_stdout, POLLIN, 0};
            const int ready = poll(&descriptor, 1, 2);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0 || (ready > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))))
                return std::nullopt;
            if (ready == 0)
                continue;
            const ssize_t count = read(m_stdout, &byte, 1);
            if (count <= 0)
                return std::nullopt;
#endif
            if (byte == '\n') {
                try {
                    return Json::parse(line);
                } catch (const Json::exception &) {
                    return std::nullopt;
                }
            }
            line.push_back(byte);
        }
        return std::nullopt;
    }

    void stop() noexcept {
#ifdef _WIN32
        if (m_stdin != nullptr) {
            CloseHandle(m_stdin);
            m_stdin = nullptr;
        }
        if (m_process != nullptr) {
            if (WaitForSingleObject(m_process, 1000) == WAIT_TIMEOUT) {
                TerminateProcess(m_process, 1);
                WaitForSingleObject(m_process, INFINITE);
            }
            CloseHandle(m_process);
            m_process = nullptr;
        }
        if (m_stdout != nullptr) {
            CloseHandle(m_stdout);
            m_stdout = nullptr;
        }
#else
        if (m_stdin >= 0) {
            close(m_stdin);
            m_stdin = -1;
        }
        if (m_pid > 0) {
            int status = 0;
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (waitpid(m_pid, &status, WNOHANG) == m_pid) {
                    m_pid = -1;
                    break;
                }
                std::this_thread::sleep_for(10ms);
            }
            if (m_pid > 0) {
                kill(m_pid, SIGKILL);
                (void)waitpid(m_pid, &status, 0);
                m_pid = -1;
            }
        }
        if (m_stdout >= 0) {
            close(m_stdout);
            m_stdout = -1;
        }
#endif
    }

  private:
#ifdef _WIN32
    HANDLE m_stdin = nullptr;
    HANDLE m_stdout = nullptr;
    HANDLE m_process = nullptr;
#else
    int m_stdin = -1;
    int m_stdout = -1;
    pid_t m_pid = -1;
#endif
};

Json request(int id, std::string method, Json params = Json::object()) {
    return Json{{"jsonrpc", "2.0"},
                {"id", id},
                {"method", std::move(method)},
                {"params", std::move(params)}};
}

} // namespace

TEST_CASE("packaged rf-sim-mcp reaches the GUI agent server and reconnects after restart",
          "[agent][mcp][packaged]") {
    ScratchDirectory scratch;
    ApiFixture fixture;
    AgentServerConfig config;
    config.endpoint_file = scratch.path / "private" / "agent-endpoint.json";
    config.app_version = "test";
    AgentServer server(fixture.api, fixture.host, config);
    std::string error;
    REQUIRE(server.start(&error));
    McpProcess bridge(config.endpoint_file);
    AgentServer *active_server = &server;
    const auto pump = [&] { active_server->pump(); };

    REQUIRE(bridge.send(request(1, "initialize",
                                {{"protocolVersion", "2025-06-18"},
                                 {"capabilities", Json::object()},
                                 {"clientInfo", {{"name", "acceptance"}, {"version", "1"}}}})));
    const auto initialized = bridge.receive(pump);
    REQUIRE(initialized.has_value());
    REQUIRE(initialized->contains("result"));
    CHECK(initialized->at("result").at("protocolVersion") == "2025-06-18");

    REQUIRE(bridge.send(request(2, "tools/list")));
    const auto listed = bridge.receive(pump);
    REQUIRE(listed.has_value());
    REQUIRE(listed->contains("result"));
    std::vector<std::string> names;
    for (const auto &tool : listed->at("result").at("tools"))
        names.push_back(tool.at("name").get<std::string>());
    CHECK(std::find(names.begin(), names.end(), "circuit_edit") != names.end());
    CHECK(std::find(names.begin(), names.end(), "circuit_get") != names.end());

    const auto callTool = [&](int id, std::string name, Json arguments) -> std::optional<Json> {
        if (!bridge.send(request(id, "tools/call",
                                 {{"name", std::move(name)}, {"arguments", std::move(arguments)}})))
            return std::nullopt;
        return bridge.receive(pump);
    };
    auto edited = callTool(
        3, "circuit_edit",
        {{"epoch", fixture.api.epoch()},
         {"ops", Json::array({{{"op", "add"}, {"ref", "from_mcp"}, {"type", "generator"}}})}});
    REQUIRE(edited.has_value());
    REQUIRE(edited->contains("result"));
    REQUIRE(edited->at("result").at("structuredContent").at("refs").contains("from_mcp"));
    const int component_id =
        edited->at("result").at("structuredContent").at("refs").at("from_mcp").get<int>();

    auto read_back = callTool(4, "circuit_get", Json::object());
    REQUIRE(read_back.has_value());
    REQUIRE(read_back->contains("result"));
    const auto &components = read_back->at("result").at("structuredContent").at("components");
    REQUIRE(components.size() == 1);
    CHECK(components[0].at("id") == component_id);
    CHECK(components[0].at("type") == "generator");

    server.stop();
    AgentServer restarted(fixture.api, fixture.host, config);
    REQUIRE(restarted.start(&error));
    active_server = &restarted;
    const auto stale_link = callTool(5, "circuit_get", Json::object());
    REQUIRE(stale_link.has_value());
    REQUIRE(stale_link->contains("result"));
    CHECK(stale_link->at("result").at("isError") == true);
    CHECK(stale_link->at("result").at("structuredContent").at("error").at("code") ==
          "SIMULATOR_UNAVAILABLE");
    auto after_restart = callTool(6, "circuit_get", Json::object());
    REQUIRE(after_restart.has_value());
    REQUIRE(after_restart->contains("result"));
    const auto &restarted_components =
        after_restart->at("result").at("structuredContent").at("components");
    REQUIRE(restarted_components.size() == 1);
    CHECK(restarted_components[0].at("id") == component_id);
    CHECK(restarted_components[0].at("type") == "generator");

    bridge.stop();
    restarted.stop();
}
