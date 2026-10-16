#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "test_temp_paths.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
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
