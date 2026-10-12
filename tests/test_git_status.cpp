#include "git_status.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

class TempTree {
  public:
    TempTree() {
        static std::atomic<unsigned> sequence{0};
        root = fs::temp_directory_path() / ("rfsim_git_status_" + test_temp_paths::processTag() +
                                            "_" + std::to_string(sequence.fetch_add(1)));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }

    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path root;
};

class ScopedCurrentPath {
  public:
    explicit ScopedCurrentPath(const fs::path &directory) : m_original(fs::current_path()) {
        fs::current_path(directory);
    }

    ~ScopedCurrentPath() {
        std::error_code ec;
        fs::current_path(m_original, ec);
    }

  private:
    fs::path m_original;
};

void runGit(const fs::path &directory, std::string_view arguments) {
    ScopedCurrentPath current_directory(directory);
    const std::string command = "git " + std::string(arguments);
    REQUIRE(std::system(command.c_str()) == 0);
}

void writeFile(const fs::path &path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.is_open());
    output << contents;
    REQUIRE(output.good());
}

void initializeRepository(const fs::path &directory, std::string_view branch) {
    fs::create_directories(directory);
    runGit(directory, "init --quiet");
    runGit(directory, "config user.name \"Issue 201 Test\"");
    runGit(directory, "config user.email issue201@example.test");
    runGit(directory, "config commit.gpgsign false");
    writeFile(directory / "staged.txt", "initial staged file\n");
    writeFile(directory / "unstaged.txt", "initial unstaged file\n");
    runGit(directory, "add staged.txt unstaged.txt");
    runGit(directory, "commit --quiet -m baseline");
    runGit(directory, "branch -M " + std::string(branch));
}

class ScopedSearchPath {
  public:
    explicit ScopedSearchPath(const fs::path &directory) : ScopedSearchPath(directory.string()) {}

    explicit ScopedSearchPath(const std::string &search_path) {
        if (const char *value = std::getenv("PATH"))
            m_original = value;
#ifdef _WIN32
        REQUIRE(_putenv_s("PATH", search_path.c_str()) == 0);
#else
        REQUIRE(setenv("PATH", search_path.c_str(), 1) == 0);
#endif
    }

    ~ScopedSearchPath() {
#ifdef _WIN32
        _putenv_s("PATH", m_original ? m_original->c_str() : "");
#else
        if (m_original)
            setenv("PATH", m_original->c_str(), 1);
        else
            unsetenv("PATH");
#endif
    }

  private:
    std::optional<std::string> m_original;
};

class ScopedEnvironmentVariable {
  public:
    ScopedEnvironmentVariable(std::string name, std::optional<std::string> value) : m_name(name) {
        if (const char *original = std::getenv(m_name.c_str()))
            m_original = original;
        set(value);
    }

    ~ScopedEnvironmentVariable() {
#ifdef _WIN32
        (void)_putenv_s(m_name.c_str(), m_original ? m_original->c_str() : "");
#else
        if (m_original)
            (void)setenv(m_name.c_str(), m_original->c_str(), 1);
        else
            (void)unsetenv(m_name.c_str());
#endif
    }

    void set(const std::optional<std::string> &value) {
#ifdef _WIN32
        REQUIRE(_putenv_s(m_name.c_str(), value ? value->c_str() : "") == 0);
#else
        if (value)
            REQUIRE(setenv(m_name.c_str(), value->c_str(), 1) == 0);
        else
            REQUIRE(unsetenv(m_name.c_str()) == 0);
#endif
    }

  private:
    std::string m_name;
    std::optional<std::string> m_original;
};

template <typename Predicate>
bool waitForGitStatus(GitStatusMonitor &monitor, const fs::path &directory, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        monitor.update(directory);
        if (predicate(monitor.status()))
            return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate(monitor.status());
}

constexpr std::string_view kHash = "0123456789abcdef0123456789abcdef01234567";

std::string headers(std::string_view oid, std::string_view branch) {
    return "# branch.oid " + std::string(oid) + "\n# branch.head " + std::string(branch) + "\n";
}

} // namespace

TEST_CASE("Git status parser formats clean, detached, and unborn repositories",
          "[git_status][parse]") {
    const auto clean = parseGitStatus(headers(kHash, "main"));
    REQUIRE(clean.has_value());
    CHECK(clean->branch == "main");
    CHECK(clean->commit == "0123456");
    CHECK(clean->staged_count == 0);
    CHECK(clean->unstaged_count == 0);
    CHECK(clean->untracked_count == 0);
    CHECK(clean->conflicted_count == 0);
    CHECK(clean->label() == "main@0123456");

    const auto detached = parseGitStatus(headers(kHash, "(detached)"));
    REQUIRE(detached.has_value());
    CHECK(detached->label() == "detached@0123456");

    const auto unborn = parseGitStatus(headers("(initial)", "issue-201-test"));
    REQUIRE(unborn.has_value());
    CHECK(unborn->branch == "issue-201-test");
    CHECK(unborn->commit.empty());
    CHECK(unborn->label() == "issue-201-test@(initial)");
}

TEST_CASE("Git status parser keeps staged, unstaged, untracked, rename, and conflict states",
          "[git_status][parse]") {
    const std::string output =
        headers(kHash, "main") +
        "1 A. N... 000000 100644 100644 0000000000000000000000000000000000000000 " +
        std::string(kHash) + " staged.txt\n" + "1 .M N... 100644 100644 100644 " +
        std::string(kHash) + " " + std::string(kHash) + " modified.txt\n" +
        "1 MM N... 100644 100644 100644 " + std::string(kHash) + " " + std::string(kHash) +
        " staged-and-modified.txt\n" + "2 R. N... 100644 100644 100644 " + std::string(kHash) +
        " " + std::string(kHash) + " R100 old-name.txt -> new-name.txt\n" +
        "u UU N... 100644 100644 100644 100644 " + std::string(kHash) + " " + std::string(kHash) +
        " " + std::string(kHash) + " conflict.txt\n" + "? untracked.txt\n";

    const auto status = parseGitStatus(output);
    REQUIRE(status.has_value());
    CHECK(status->staged_count == 3);
    CHECK(status->unstaged_count == 2);
    CHECK(status->untracked_count == 1);
    CHECK(status->conflicted_count == 1);
    CHECK(status->label() == "main@0123456 +3 ~2 ?1 !1");
}

TEST_CASE("Git header label fits narrow widths while preserving the commit and change summary",
          "[git_status][ui]") {
    GitStatus status;
    status.branch = std::string(80, 'b');
    status.commit = "0123456";
    status.staged_count = 1;
    status.unstaged_count = 2;
    status.untracked_count = 3;
    const auto measure = [](std::string_view text) { return static_cast<float>(text.size() * 8); };

    const auto narrow_git = fitGitStatusHeaderLabel(status, 192.0f, measure);
    CHECK(narrow_git == "Git: 0123456 +1 ~2 ?3");
    CHECK(measure(narrow_git) <= 192.0f);
    CHECK(fitGitStatusHeaderLabel(status, 208.0f, measure) == "Git: 0123456 +1 ~2 ?3 b...");
    const float commit_only_width = measure("Git: " + status.commit);
    CHECK(fitGitStatusHeaderLabel(status, commit_only_width, measure) == "Git: 0123456");
    CHECK(fitGitStatusHeaderLabel(status, commit_only_width + measure("..."), measure) ==
          "Git: 0123456");
    const auto measure_utf8 = [](std::string_view text) {
        float width = 0.0f;
        for (const char byte : text) {
            if ((static_cast<unsigned char>(byte) & 0xC0) != 0x80)
                width += 8.0f;
        }
        return width;
    };
    CHECK(fitTextToWidth("éabcdef", 40.0f, measure_utf8) == "éa...");
    CHECK(fitTextToWidth("Circuit_A.rfsim", 88.0f, measure) == "Circuit_...");
    CHECK(fitGitStatusHeaderLabel(status, 1200.0f, measure) ==
          "Git: 0123456 +1 ~2 ?3 " + status.branch);
    const std::string project_title = "Circuit_File_Name_That_Is_Much_Longer.rfsim";
    const std::string fps_title = "30.0 FPS";
    constexpr float spacing = 8.0f;
    constexpr float project_width = 88.0f;
    const float required_width =
        project_width + measure("Git: 0123456 +1 ~2 ?3") + measure(fps_title) + 2.0f * spacing;
    const std::optional<GitStatus> status_snapshot{status};
    const auto labels = fitGitHeaderLabels(project_title, status_snapshot, required_width,
                                           measure(fps_title), spacing, measure);
    CHECK(labels.project_title == "Circuit_...");
    CHECK(labels.git_title == "Git: 0123456 +1 ~2 ?3");
    CHECK(measure(labels.project_title) + measure(labels.git_title) + measure(fps_title) +
              2.0f * spacing <=
          required_width);
}

TEST_CASE("Git status query handles a spaced project path and actual index/worktree state",
          "[git_status][query]") {
    TempTree tree;
    const fs::path repository = tree.root / "repository with spaces";
    initializeRepository(repository, "issue-201-test");

    writeFile(repository / "staged.txt", "staged change\n");
    runGit(repository, "add staged.txt");
    writeFile(repository / "unstaged.txt", "unstaged change\n");
    writeFile(repository / "untracked.txt", "new file\n");

    const auto status = queryGitStatus(repository);
    REQUIRE(status.has_value());
    CHECK(status->branch == "issue-201-test");
    CHECK(status->commit.size() == 7);
    CHECK(status->staged_count == 1);
    CHECK(status->unstaged_count == 1);
    CHECK(status->untracked_count == 1);
}

#ifndef _WIN32
TEST_CASE("Git status query preserves stdout when standard input and output are closed",
          "[git_status][query][posix]") {
    ScopedEnvironmentVariable git_dir("GIT_DIR", std::nullopt);
    ScopedEnvironmentVariable git_work_tree("GIT_WORK_TREE", std::nullopt);
    TempTree tree;
    const fs::path repository = tree.root / "closed-descriptors";
    initializeRepository(repository, "closed-fd-project");

    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        const auto status = queryGitStatus(repository);
        _exit(status && status->branch == "closed-fd-project" ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    int child_status = 0;
    REQUIRE(waitpid(child, &child_status, 0) == child);
    REQUIRE(WIFEXITED(child_status));
    CHECK(WEXITSTATUS(child_status) == EXIT_SUCCESS);
}
#endif

TEST_CASE("Git status query ignores inherited repository-selection overrides",
          "[git_status][query]") {
    ScopedEnvironmentVariable git_dir("GIT_DIR", std::nullopt);
    ScopedEnvironmentVariable git_work_tree("GIT_WORK_TREE", std::nullopt);
    TempTree tree;
    const fs::path active_repository = tree.root / "active";
    const fs::path redirected_repository = tree.root / "redirected";
    initializeRepository(active_repository, "active-project");
    initializeRepository(redirected_repository, "redirected-project");
    writeFile(active_repository / "unstaged.txt", "active edit\n");

    git_dir.set((redirected_repository / ".git").string());
    git_work_tree.set(redirected_repository.string());
    const auto status = queryGitStatus(active_repository);
    REQUIRE(status.has_value());
    CHECK(status->branch == "active-project");
    CHECK(status->unstaged_count == 1);
}

TEST_CASE("Git status query is unavailable outside a repository or without Git on PATH",
          "[git_status][query]") {
    TempTree tree;
    const fs::path repository = tree.root / "repository";
    initializeRepository(repository, "issue-201-test");
    const fs::path empty_path = tree.root / "empty-path";
    fs::create_directories(empty_path);

    CHECK_FALSE(queryGitStatus(tree.root).has_value());
    {
        ScopedSearchPath no_git(empty_path);
        CHECK_FALSE(queryGitStatus(repository).has_value());
    }

    GitStatusMonitor monitor;
    monitor.update(tree.root);
    CHECK(monitor.label() == "Git unavailable");
}

TEST_CASE("Git status query stops when Git exceeds its process deadline", "[git_status][timeout]") {
    TempTree tree;
    const fs::path repository = tree.root / "repository";
    initializeRepository(repository, "issue-201-test");
    const fs::path fake_bin = tree.root / "slow-git";
    fs::create_directories(fake_bin);

    const fs::path helper(GIT_STATUS_HANG_HELPER);
    REQUIRE(fs::is_regular_file(helper));
#ifdef _WIN32
    const fs::path fake_git = fake_bin / "git.exe";
#else
    const fs::path fake_git = fake_bin / "git";
#endif
    fs::copy_file(helper, fake_git);
#ifndef _WIN32
    fs::permissions(fake_git, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                  fs::perms::others_read | fs::perms::others_exec);
#endif

    std::string search_path = fake_bin.string();
    if (const char *current_path = std::getenv("PATH")) {
        if (*current_path != '\0') {
#ifdef _WIN32
            search_path += ';';
#else
            search_path += ':';
#endif
            search_path += current_path;
        }
    }
    ScopedSearchPath slow_git(search_path);

    const auto started = std::chrono::steady_clock::now();
    const auto status = queryGitStatus(repository);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK_FALSE(status.has_value());
    CHECK(elapsed >= 250ms);
    CHECK(elapsed < 2s);
#ifdef _WIN32
    std::ifstream child_pid_file(repository / "git_status_child.pid");
    REQUIRE(child_pid_file.is_open());
    DWORD child_pid = 0;
    child_pid_file >> child_pid;
    REQUIRE(child_pid != 0);

    HANDLE child_process = OpenProcess(SYNCHRONIZE, FALSE, child_pid);
    if (child_process != nullptr) {
        CHECK(WaitForSingleObject(child_process, 0) == WAIT_OBJECT_0);
        CloseHandle(child_process);
    } else {
        CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    }
#endif
    GitStatusMonitor monitor;
    const auto monitor_started = std::chrono::steady_clock::now();
    monitor.update(repository);
    const auto monitor_elapsed = std::chrono::steady_clock::now() - monitor_started;
    CHECK(monitor_elapsed < 250ms);
    CHECK_FALSE(monitor.status().has_value());
}

TEST_CASE("Git status monitor refreshes when the project changes and after two seconds",
          "[git_status][monitor]") {
    TempTree tree;
    const fs::path first_repository = tree.root / "first";
    const fs::path second_repository = tree.root / "second";
    initializeRepository(first_repository, "first-project");
    initializeRepository(second_repository, "second-project");

    GitStatusMonitor monitor;
    CHECK(waitForGitStatus(monitor, first_repository, [](const std::optional<GitStatus> &status) {
        return status.has_value();
    }));
    REQUIRE(monitor.status().has_value());
    CHECK(monitor.status()->branch == "first-project");
    CHECK(monitor.status()->unstaged_count == 0);

    writeFile(first_repository / "unstaged.txt", "edited\n");
    monitor.update(first_repository);
    REQUIRE(monitor.status().has_value());
    CHECK(monitor.status()->unstaged_count == 0);

    std::this_thread::sleep_for(2100ms);
    monitor.update(first_repository);
    CHECK(waitForGitStatus(monitor, first_repository, [](const std::optional<GitStatus> &status) {
        return status.has_value() && status->unstaged_count == 1;
    }));
    REQUIRE(monitor.status().has_value());
    CHECK(monitor.status()->unstaged_count == 1);

    monitor.update(second_repository);
    CHECK(waitForGitStatus(monitor, second_repository, [](const std::optional<GitStatus> &status) {
        return status.has_value() && status->branch == "second-project";
    }));
    REQUIRE(monitor.status().has_value());
    CHECK(monitor.status()->branch == "second-project");
    CHECK(monitor.status()->unstaged_count == 0);
}

TEST_CASE("Git status monitor rejects stale results after a rapid A-B-A project switch",
          "[git_status][monitor]") {
    TempTree tree;
    const fs::path active = tree.root / "active";
    const fs::path other = tree.root / "other";
    fs::create_directories(active);
    fs::create_directories(other);
    writeFile(active / "git_status_branch.txt", "old-project\n");
    writeFile(other / "git_status_branch.txt", "other-project\n");

    const fs::path fake_bin = tree.root / "delayed-git";
    fs::create_directories(fake_bin);
    const fs::path helper(GIT_STATUS_HANG_HELPER);
    REQUIRE(fs::is_regular_file(helper));
#ifdef _WIN32
    const fs::path fake_git = fake_bin / "git.exe";
#else
    const fs::path fake_git = fake_bin / "git";
#endif
    fs::copy_file(helper, fake_git);
#ifndef _WIN32
    fs::permissions(fake_git, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                  fs::perms::others_read | fs::perms::others_exec);
#endif

    std::string search_path = fake_bin.string();
    if (const char *current_path = std::getenv("PATH")) {
        if (*current_path != '\0') {
#ifdef _WIN32
            search_path += ';';
#else
            search_path += ':';
#endif
            search_path += current_path;
        }
    }
    ScopedSearchPath delayed_git(search_path);
    ScopedEnvironmentVariable helper_mode("RFSIM_GIT_STATUS_HELPER_MODE",
                                          std::optional<std::string>{"delayed-status"});

    const auto wait_for_file = [](const fs::path &path, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (fs::exists(path))
                return true;
            std::this_thread::sleep_for(5ms);
        }
        return fs::exists(path);
    };

    GitStatusMonitor monitor;
    const fs::path active_started = active / "git_status_helper_started";
    monitor.update(active);
    REQUIRE(wait_for_file(active_started, 2s));

    writeFile(active / "git_status_branch.txt", "new-project\n");
    fs::remove(active_started);
    monitor.update(other);
    monitor.update(active);
    CHECK(wait_for_file(active_started, 250ms));

    bool saw_stale_result = false;
    CHECK(waitForGitStatus(monitor, active, [&](const std::optional<GitStatus> &status) {
        if (status && status->branch == "old-project")
            saw_stale_result = true;
        return status && status->branch == "new-project";
    }));
    CHECK_FALSE(saw_stale_result);
}
