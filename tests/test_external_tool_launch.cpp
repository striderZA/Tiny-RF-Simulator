#include "agent_socket.h"
#include "extension_manifest.h"
#include "external_tool_runner.h"
#include "test_temp_paths.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ScopedRemove {
    fs::path path;

    ~ScopedRemove() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

ExtensionManifest makeExternalTool(const fs::path &root, const fs::path &entry_path) {
    ExtensionManifest manifest;
    manifest.id = "vendor.launch-probe";
    manifest.name = "Launch probe";
    manifest.version = "1.0.0";
    manifest.kind = ExtensionKind::ExternalTool;
    manifest.root_dir = root;
    manifest.entry_path = entry_path;
    return manifest;
}

void writeExecutableScript(const fs::path &path, const std::string &body) {
    fs::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        out << body;
    }
    fs::permissions(path, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                              fs::perms::others_read | fs::perms::others_exec);
}

// Empties a scratch root left by an earlier run with the same process id, so stale
// files cannot satisfy a check.
void clearScratch(const fs::path &root) {
    std::error_code ec;
    fs::remove_all(root, ec);
}

#if defined(__linux__)
// "socket:[inode]" link targets for every socket this process currently holds.
std::set<std::string> openSocketLinks() {
    std::set<std::string> links;
    for (const auto &entry : fs::directory_iterator("/proc/self/fd")) {
        std::error_code ec;
        const std::string target = fs::read_symlink(entry.path(), ec).string();
        if (!ec && target.rfind("socket:", 0) == 0)
            links.insert(target);
    }
    return links;
}

TEST_CASE("external tool launch does not inherit open agent sockets",
          "[extensions][runner][launch]") {
    // Regression: the GUI-side listener and accepted channel are plain sockets
    // unless they are close-on-exec, so every tool launched while the agent
    // server holds them would inherit them across exec().
    const auto sockets_before = openSocketLinks();

    std::string error;
    auto listener = AgentListener::bindLoopback(&error);
    REQUIRE(listener.has_value());
    auto client = connectAgentLoopback(listener->port(), std::chrono::seconds(2), &error);
    REQUIRE(client.has_value());
    auto accepted = listener->accept(std::chrono::seconds(2));
    REQUIRE(accepted.has_value());
    // The bridge-side client is outside this contract; only GUI-side sockets are tested.
    client->close();

    std::set<std::string> agent_sockets;
    for (const auto &link : openSocketLinks()) {
        if (sockets_before.count(link) == 0)
            agent_sockets.insert(link);
    }
    REQUIRE(agent_sockets.size() == 2); // listener + accepted channel, both still open

    const fs::path work_root =
        fs::temp_directory_path() / ("rfsim_launch_fds_" + test_temp_paths::processTag());
    clearScratch(work_root);
    ScopedRemove cleanup{work_root};
    const fs::path script = work_root / "tool" / "list_fds.sh";
    writeExecutableScript(script, R"SH(#!/bin/sh
result=
while [ "$#" -gt 0 ]; do
  if [ "$1" = "--result" ]; then result=$2; fi
  shift
done
dir=$(dirname "$result")
: > "$dir/fds.txt"
for fd in /proc/$$/fd/*; do
  readlink "$fd" >> "$dir/fds.txt"
done
printf '{"message":"ok"}' > "$result"
)SH");

    ExternalToolRequest request;
    request.contract_version = "1";
    request.action_label = "List fds";
    request.project_root = work_root;
    request.selected_path = work_root / "selection.s2p";
    request.work_dir = work_root / "runs";

    const ExternalToolRunResult result =
        ExternalToolRunner{}.run(makeExternalTool(script.parent_path(), script), request);
    INFO("runner message: " << result.message);
    REQUIRE(result.ok);

    std::set<std::string> inherited;
    std::size_t entries = 0;
    std::ifstream listing(result.work_dir / "fds.txt");
    for (std::string line; std::getline(listing, line);) {
        ++entries;
        if (agent_sockets.count(line) != 0)
            inherited.insert(line);
    }
    REQUIRE(entries > 0);

    std::string leaked;
    for (const auto &link : inherited)
        leaked += link + " ";
    INFO("tool inherited agent sockets: " << leaked);
    CHECK(inherited.empty());
}
#endif

// Sets an environment variable for one test and restores its previous value, or its
// absence, when the object goes out of scope.
class ScopedEnv {
  public:
    ScopedEnv(std::string name, const std::string &value) : m_name(std::move(name)) {
        if (const char *previous = std::getenv(m_name.c_str()))
            m_previous = previous;
        setenv(m_name.c_str(), value.c_str(), 1);
    }
    ~ScopedEnv() {
        if (m_previous)
            setenv(m_name.c_str(), m_previous->c_str(), 1);
        else
            unsetenv(m_name.c_str());
    }
    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

  private:
    std::string m_name;
    std::optional<std::string> m_previous;
};

ExternalToolRequest makeRequest(const fs::path &work_root) {
    ExternalToolRequest request;
    request.contract_version = "1";
    request.action_label = "Probe";
    request.project_root = work_root;
    request.selected_path = work_root / "selection.s2p";
    request.work_dir = work_root / "runs";
    return request;
}

std::vector<std::string> readLines(const fs::path &path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);)
        lines.push_back(line);
    return lines;
}

// Records how the runner invoked the tool (working directory, an environment
// marker, every argument), then writes a successful result.
constexpr const char *kProbeToolScript = R"SH(#!/bin/sh
result=
prev=
for arg in "$@"; do
  if [ "$prev" = "--result" ]; then result=$arg; fi
  prev=$arg
done
dir=$(dirname "$result")
{
  printf 'cwd=%s\n' "$(pwd -P)"
  printf 'marker=%s\n' "$RFSIM_LAUNCH_PROBE"
  for arg in "$@"; do printf 'arg=%s\n' "$arg"; done
} > "$dir/probe.txt"
printf '{"message":"ok"}' > "$result"
)SH";

TEST_CASE("external tool launch runs in its workspace with arguments and environment",
          "[extensions][runner][launch]") {
    // Characterizes the exec path: working directory, argv, and environment
    // reach the tool exactly as the runner documents them.
    const fs::path work_root =
        fs::temp_directory_path() / ("rfsim_launch_probe_" + test_temp_paths::processTag());
    clearScratch(work_root);
    ScopedRemove cleanup{work_root};
    const fs::path script = work_root / "tool" / "probe.sh";
    writeExecutableScript(script, kProbeToolScript);

    const ScopedEnv marker{"RFSIM_LAUNCH_PROBE", "marker-value"};
    const ExternalToolRunResult result = ExternalToolRunner{}.run(
        makeExternalTool(script.parent_path(), script), makeRequest(work_root));
    INFO("runner message: " << result.message);
    REQUIRE(result.ok);

    const auto lines = readLines(result.work_dir / "probe.txt");
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "cwd=" + fs::canonical(result.work_dir).string());
    CHECK(lines[1] == "marker=marker-value");
    CHECK(lines[2] == "arg=--request");
    // The request file's name is a serialization detail. The tool must receive a
    // readable request for this action, in this run's workspace.
    REQUIRE(lines[3].rfind("arg=", 0) == 0);
    const fs::path request_file = lines[3].substr(4);
    CHECK(request_file.parent_path() == result.work_dir);
    REQUIRE(fs::is_regular_file(request_file));
    std::ifstream request_in(request_file);
    CHECK(nlohmann::json::parse(request_in).at("action_label").get<std::string>() == "Probe");
    CHECK(lines[4] == "arg=--result");
    CHECK(lines[5] == "arg=" + result.result_path.string());
}

TEST_CASE("external tool without a shebang still runs through the shell",
          "[extensions][runner][launch]") {
    // execvp() ran files the kernel refuses with ENOEXEC through /bin/sh; the
    // launch path has to keep doing so.
    const fs::path work_root =
        fs::temp_directory_path() / ("rfsim_launch_noshebang_" + test_temp_paths::processTag());
    clearScratch(work_root);
    ScopedRemove cleanup{work_root};
    const fs::path script = work_root / "tool" / "no_shebang.sh";
    writeExecutableScript(script, R"SH(result=
prev=
for arg in "$@"; do
  if [ "$prev" = "--result" ]; then result=$arg; fi
  prev=$arg
done
printf '{"message":"ok"}' > "$result"
)SH");

    const ExternalToolRunResult result = ExternalToolRunner{}.run(
        makeExternalTool(script.parent_path(), script), makeRequest(work_root));
    INFO("runner message: " << result.message);
    CHECK(result.ok);
    CHECK(result.message == "ok");
}

TEST_CASE("external tool without execute permission is never run", "[extensions][runner][launch]") {
    const fs::path work_root =
        fs::temp_directory_path() / ("rfsim_launch_noexec_" + test_temp_paths::processTag());
    clearScratch(work_root);
    ScopedRemove cleanup{work_root};
    const fs::path script = work_root / "tool" / "not_executable.sh";
    fs::create_directories(script.parent_path());
    {
        std::ofstream out(script);
        out << kProbeToolScript;
    }

    const ExternalToolRunResult result = ExternalToolRunner{}.run(
        makeExternalTool(script.parent_path(), script), makeRequest(work_root));
    CHECK_FALSE(result.ok);
    CHECK(result.exit_code == 127);
    CHECK_FALSE(fs::exists(result.result_path));
}

TEST_CASE("external tool launch skips PATH entries it cannot run and keeps searching",
          "[extensions][runner][launch]") {
    // A .py entry runs as a bare "python3", which the launch resolves over PATH. An
    // earlier PATH entry that is missing or holds a non-executable file must not end
    // the search before a later executable candidate.
    const fs::path work_root =
        fs::temp_directory_path() / ("rfsim_launch_path_" + test_temp_paths::processTag());
    clearScratch(work_root);
    ScopedRemove cleanup{work_root};

    const fs::path missing_dir = work_root / "missing";
    const fs::path blocked_dir = work_root / "blocked";
    const fs::path runnable_dir = work_root / "runnable";
    fs::create_directories(blocked_dir);
    {
        std::ofstream out(blocked_dir / "python3");
        out << "not executable\n";
    }
    fs::permissions(blocked_dir / "python3", fs::perms::owner_read | fs::perms::owner_write);
    writeExecutableScript(runnable_dir / "python3", R"SH(#!/bin/sh
result=
prev=
for arg in "$@"; do
  if [ "$prev" = "--result" ]; then result=$arg; fi
  prev=$arg
done
printf 'runnable\n' > "${result%/*}/which.txt"
printf '{"message":"ok"}' > "$result"
)SH");

    const fs::path entry = work_root / "tool" / "probe.py";
    fs::create_directories(entry.parent_path());
    {
        std::ofstream out(entry);
        out << "# launched through the python3 found on PATH\n";
    }

    const char *inherited_path = std::getenv("PATH");
    const std::string search_path = missing_dir.string() + ":" + blocked_dir.string() + ":" +
                                    runnable_dir.string() + ":" +
                                    (inherited_path != nullptr ? inherited_path : "/usr/bin:/bin");
    const ScopedEnv path_override{"PATH", search_path};

    const ExternalToolRunResult result = ExternalToolRunner{}.run(
        makeExternalTool(entry.parent_path(), entry), makeRequest(work_root));
    INFO("runner message: " << result.message);
    REQUIRE(result.ok);
    CHECK(readLines(result.work_dir / "which.txt") == std::vector<std::string>{"runnable"});
}

} // namespace
