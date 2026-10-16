#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
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
        static std::mt19937_64 rng{std::random_device{}()};
        dir = fs::temp_directory_path() /
              ("rf_issue224_" + std::string(stem) + "_" + std::to_string(rng()));
        std::error_code ec;
        fs::remove_all(dir, ec);
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
    try {
        fs::create_symlink(victim, temp);
    } catch (const fs::filesystem_error &e) {
        WARN("Symbolic-link test skipped: creating a symlink is unavailable on this system: "
             << e.what());
        return;
    }

    RfSimulatorApp app;
    app.newProject();
    app.testCreateComponent("generator", 22402);

    REQUIRE_FALSE(app.saveProject(target.string()));
    REQUIRE(readText(victim) == "KEEP");
    REQUIRE(readText(target) == original_target);
    REQUIRE(fs::is_symlink(fs::symlink_status(temp)));
}
