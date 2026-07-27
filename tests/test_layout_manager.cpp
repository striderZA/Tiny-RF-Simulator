#include "layout_manager.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

TEST_CASE("LayoutManager derives non-empty exe-relative paths", "[layout]") {
    LayoutManager mgr;
    std::string default_path = mgr.defaultLayoutPath();
    std::string layouts_dir = mgr.layoutsDir();

    REQUIRE_FALSE(default_path.empty());
    REQUIRE_FALSE(layouts_dir.empty());
    REQUIRE(std::filesystem::path(default_path).filename().string() == "rf_simulator_layout.ini");
    REQUIRE(std::filesystem::path(layouts_dir).filename().string() == "layouts");
    // Both live in the same parent directory (the exe's directory).
    REQUIRE(std::filesystem::path(default_path).parent_path() ==
            std::filesystem::path(layouts_dir).parent_path());
}

TEST_CASE("LayoutManager::sanitizeName strips unsafe characters", "[layout]") {
    REQUIRE(LayoutManager::sanitizeName("My Layout") == "My Layout");
    REQUIRE(LayoutManager::sanitizeName("../../etc/passwd") == "etcpasswd");
    REQUIRE(LayoutManager::sanitizeName("a/b\\c:d*e") == "abcde");
    REQUIRE(LayoutManager::sanitizeName("  padded  ") == "padded");
    REQUIRE(LayoutManager::sanitizeName("///") == "Layout");
    REQUIRE(LayoutManager::sanitizeName("") == "Layout");
}

TEST_CASE("LayoutManager named-preset filesystem operations", "[layout]") {
    LayoutManager mgr;
    // Clean slate for this test's presets.
    std::filesystem::remove_all(mgr.layoutsDir());

    SECTION("namedLayoutExists is false before creation") {
        REQUIRE_FALSE(mgr.namedLayoutExists("Test Preset"));
    }

    SECTION("listNamedLayouts is empty when layoutsDir doesn't exist") {
        REQUIRE(mgr.listNamedLayouts().empty());
    }

    SECTION("listNamedLayouts, namedLayoutExists, deleteNamedLayout round-trip") {
        std::filesystem::create_directories(mgr.layoutsDir());
        {
            std::ofstream ofs(std::filesystem::path(mgr.layoutsDir()) / "Test Preset.ini");
            ofs << "[Window][Test]\nPos=0,0\n";
        }
        REQUIRE(mgr.namedLayoutExists("Test Preset"));
        auto names = mgr.listNamedLayouts();
        REQUIRE(names.size() == 1);
        REQUIRE(names[0] == "Test Preset");

        REQUIRE(mgr.deleteNamedLayout("Test Preset"));
        REQUIRE_FALSE(mgr.namedLayoutExists("Test Preset"));
        REQUIRE(mgr.listNamedLayouts().empty());
    }

    SECTION("deleteNamedLayout returns false for a missing preset") {
        REQUIRE_FALSE(mgr.deleteNamedLayout("Does Not Exist"));
    }

    SECTION("renameNamedLayout round-trip") {
        std::filesystem::create_directories(mgr.layoutsDir());
        {
            std::ofstream ofs(std::filesystem::path(mgr.layoutsDir()) / "Old Name.ini");
            ofs << "[Window][Test]\nPos=0,0\n";
        }
        REQUIRE(mgr.renameNamedLayout("Old Name", "New Name"));
        REQUIRE_FALSE(mgr.namedLayoutExists("Old Name"));
        REQUIRE(mgr.namedLayoutExists("New Name"));
    }

    SECTION("renameNamedLayout fails when source is missing") {
        REQUIRE_FALSE(mgr.renameNamedLayout("Nonexistent", "Whatever"));
    }

    SECTION("renameNamedLayout fails when destination already exists") {
        std::filesystem::create_directories(mgr.layoutsDir());
        {
            std::ofstream ofs1(std::filesystem::path(mgr.layoutsDir()) / "A.ini");
            ofs1 << "[Window][A]\n";
            std::ofstream ofs2(std::filesystem::path(mgr.layoutsDir()) / "B.ini");
            ofs2 << "[Window][B]\n";
        }
        REQUIRE_FALSE(mgr.renameNamedLayout("A", "B"));
        REQUIRE(mgr.namedLayoutExists("A"));
        REQUIRE(mgr.namedLayoutExists("B"));
    }

    std::filesystem::remove_all(mgr.layoutsDir());
}
