#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "logging_core.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif
#include "test_temp_paths.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

struct TempTree {
    fs::path dir;
    explicit TempTree(const char *stem) {
        static unsigned int next_tree = 0;
        const auto suffix = test_temp_paths::processTag() + "_" + std::to_string(next_tree++);
        dir = fs::temp_directory_path() / ("rf_issue224_" + std::string(stem) + "_" + suffix);
        std::error_code ec;
        fs::remove_all(dir, ec);
        ec.clear();
        fs::create_directories(dir, ec);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path file(const std::string &name) const { return dir / name; }
};

void writeText(const fs::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void createFileSymlinkOrSkip(const fs::path &target, const fs::path &link) {
#ifdef _WIN32
    constexpr DWORD allow_unprivileged_create = 0x2;
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), allow_unprivileged_create))
        return;

    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) {
        if (CreateSymbolicLinkW(link.c_str(), target.c_str(), 0))
            return;
        error = GetLastError();
    }
    if (error == ERROR_PRIVILEGE_NOT_HELD)
        SKIP("Windows did not grant permission to create file symlinks");
    FAIL("CreateSymbolicLinkW failed with error " + std::to_string(error));
#else
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    if (ec)
        SKIP("File symlinks are unavailable: " + ec.message());
#endif
}

bool isSymlinkPath(const fs::path &path) {
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}

// Makes the stale "<target>.tmp" impossible to remove, so the save's removal fails.
// Windows holds it open without FILE_SHARE_DELETE; POSIX makes its directory
// read-only, so unlink() is refused. The destructor restores the state.
struct StaleTempBlocker {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    StaleTempBlocker(const fs::path &temp, const fs::path &) {
        handle = CreateFileW(temp.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error("could not hold the stale temp file open");
    }
    ~StaleTempBlocker() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    fs::path dir;
    StaleTempBlocker(const fs::path &, const fs::path &directory) : dir(directory) {
        fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec,
                        fs::perm_options::replace);
    }
    ~StaleTempBlocker() {
        std::error_code ec;
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    }
#endif
    StaleTempBlocker(const StaleTempBlocker &) = delete;
    StaleTempBlocker &operator=(const StaleTempBlocker &) = delete;
};

} // namespace

TEST_CASE_METHOD(ImGuiFixture, "Issue #224: save replaces a hard link without changing its victim",
                 "[issue224][project]") {
    TempTree tree("hard_link");
    const auto target = tree.file("project.rfsim");
    const auto temp = fs::path(target.string() + ".tmp");
    const auto victim = tree.file("victim.txt");
    writeText(victim, "KEEP");
    fs::create_hard_link(victim, temp);

    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 22401);

    REQUIRE(app.saveProject(target.string()));
    REQUIRE(readText(victim) == "KEEP");
    REQUIRE_FALSE(fs::exists(temp));

    RfSimulatorApp loaded;
    REQUIRE(loaded.loadProject(target.string()));
    REQUIRE(loaded.componentCount() == 1);
}

TEST_CASE_METHOD(ImGuiFixture, "Issue #224: save refuses a symbolic link at the temp path",
                 "[issue224][project]") {
    TempTree tree("symbolic_link");
    const auto target = tree.file("project.rfsim");
    const auto temp = fs::path(target.string() + ".tmp");
    const auto victim = tree.file("victim.txt");
    const std::string original_target = "ORIGINAL TARGET";
    writeText(victim, "KEEP");
    writeText(target, original_target);
    createFileSymlinkOrSkip(victim, temp);

    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 22402);

    REQUIRE_FALSE(app.saveProject(target.string()));
    REQUIRE(readText(victim) == "KEEP");
    REQUIRE(readText(target) == original_target);
    REQUIRE(isSymlinkPath(temp));
}

TEST_CASE_METHOD(ImGuiFixture, "Issue #224: save refuses a dangling symbolic link at the temp path",
                 "[issue224][project]") {
    TempTree tree("dangling_symbolic_link");
    const auto target = tree.file("project.rfsim");
    const auto temp = fs::path(target.string() + ".tmp");
    const auto missing = tree.file("missing-victim.txt");
    const std::string original_target = "ORIGINAL TARGET";
    writeText(target, original_target);

    createFileSymlinkOrSkip(missing, temp);

    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 22403);

    REQUIRE_FALSE(app.saveProject(target.string()));
    REQUIRE_FALSE(fs::exists(missing));
    REQUIRE(readText(target) == original_target);
    REQUIRE(isSymlinkPath(temp));
}

TEST_CASE_METHOD(ImGuiFixture,
                 "Issue #224: save replaces a stale regular temp file and leaves no temp behind",
                 "[issue224][project]") {
    TempTree tree("stale_regular");
    const auto target = tree.file("project.rfsim");
    const auto temp = fs::path(target.string() + ".tmp");
    writeText(temp, "STALE");

    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 22404);

    REQUIRE(app.saveProject(target.string()));
    REQUIRE_FALSE(fs::exists(temp));

    RfSimulatorApp loaded;
    REQUIRE(loaded.loadProject(target.string()));
    REQUIRE(loaded.componentCount() == 1);
}

TEST_CASE_METHOD(
    ImGuiFixture,
    "Issue #224: save keeps the previous project when the stale temp file cannot be removed",
    "[issue224][project]") {
#ifndef _WIN32
    if (geteuid() == 0)
        SKIP("root ignores the directory write permission this case relies on");
#endif
    TempTree tree("stale_unremovable");
    const auto target = tree.file("project.rfsim");
    const auto temp = fs::path(target.string() + ".tmp");

    RfSimulatorApp previous;
    previous.newProject();
    previous.testCreateComponent("generator", 22405);
    REQUIRE(previous.saveProject(target.string()));
    const std::string before = readText(target);

    writeText(temp, "STALE");
    {
        StaleTempBlocker blocker(temp, tree.dir);

        RfSimulatorApp app;
        app.newProject();
        app.testCreateComponent("generator", 22406);

        REQUIRE_FALSE(app.saveProject(target.string()));
        REQUIRE(readText(target) == before);
        REQUIRE(fs::exists(temp));
    }

    // With the blocker released, the same stale file is removed and the save succeeds,
    // so the refusal above came from the held file and not from another save failure.
    RfSimulatorApp retry;
    retry.newProject();
    retry.testCreateComponent("generator", 22407);
    REQUIRE(retry.saveProject(target.string()));
    REQUIRE_FALSE(fs::exists(temp));
}

TEST_CASE_METHOD(ImGuiFixture, "Issue #224: save refuses a folder at the temp path and keeps it",
                 "[issue224][project][folder]") {
    TempTree tree("folder_temp");
    const auto target = tree.file("project.rfsim");
    const auto temp = fs::path(target.string() + ".tmp");

    RfSimulatorApp previous;
    previous.newProject();
    previous.testCreateComponent("generator", 22408);
    REQUIRE(previous.saveProject(target.string()));
    const std::string before = readText(target);

    fs::create_directories(temp);
    writeText(temp / "keep.txt", "KEEP");

    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 22409);
    LoggerCore::instance().clear();
    REQUIRE_FALSE(app.saveProject(target.string()));

    REQUIRE(readText(target) == before);
    REQUIRE(fs::is_directory(temp));
    REQUIRE(readText(temp / "keep.txt") == "KEEP");

    bool refused = false;
    for (const LogEntry &entry : LoggerCore::instance().entries())
        if (entry.level == Level::Error &&
            entry.message.find("is not a regular file") != std::string::npos)
            refused = true;
    REQUIRE(refused);
}
