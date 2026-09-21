// Regression for issue #130: a menu label is the action's only identifier at
// run time (ExternalToolRequest::action_label) and the ImGui item is keyed on
// it, so two entries sharing a (location, label) pair cannot be told apart once
// clicked. `parseMenus()` rejects the duplicate rather than keeping an entry
// that cannot be addressed.
//
// Standalone executable: the MinGW-w64 registration ceiling keeps new TEST_CASEs
// out of the main tests binary, and this also stays out of test_extensions.cpp,
// following test_issue80_extension_hardening.

#include "extension_manifest.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path writeManifest(const fs::path &dir, const std::string &body) {
    fs::create_directories(dir);
    const fs::path path = dir / "plugin.json";
    std::ofstream out(path);
    out << body;
    return path;
}

struct ScopedRemove {
    fs::path path;

    ~ScopedRemove() {
        std::error_code ec;
        fs::remove_all(path, ec);
        if (ec)
            WARN("ScopedRemove failed to remove " << path.string() << ": " << ec.message());
    }
};

} // namespace

TEST_CASE("extension manifest rejects duplicate menu labels", "[issue130][extensions][manifest]") {
    const fs::path root = fs::temp_directory_path() / "rfsim_ext_manifest_dup_label";
    ScopedRemove cleanup{root};

    const auto parse = [&root](const std::string &menus,
                               std::vector<ExtensionValidationIssue> &issues) {
        const fs::path manifest_path = writeManifest(root, R"json({
            "schema_version": 1,
            "id": "vendor.dup-label",
            "name": "Dup Label",
            "version": "1.0.0",
            "kind": "external-tool",
            "entry": "bin/tool.py",
            "menus": )json" + menus + "}");
        return parseExtensionManifest(manifest_path, issues);
    };

    SECTION("the same label in the same location is rejected") {
        std::vector<ExtensionValidationIssue> issues;
        const auto manifest = parse(R"json([{"location": "tools", "label": "Run"},
                          {"location": "tools", "label": "Run"}])json",
                                    issues);

        REQUIRE_FALSE(manifest.has_value());
        REQUIRE_FALSE(issues.empty());
        REQUIRE(issues.front().field == "menus[1].label");
    }

    SECTION("the same label in a different location is allowed") {
        std::vector<ExtensionValidationIssue> issues;
        const auto manifest = parse(R"json([{"location": "tools", "label": "Run"},
                          {"location": "toolbar", "label": "Run"}])json",
                                    issues);

        REQUIRE(manifest.has_value());
        REQUIRE(issues.empty());
        REQUIRE(manifest->menus.size() == 2);
    }

    SECTION("distinct labels in one location are allowed") {
        std::vector<ExtensionValidationIssue> issues;
        const auto manifest = parse(R"json([{"location": "tools", "label": "Import"},
                          {"location": "tools", "label": "Export"}])json",
                                    issues);

        REQUIRE(manifest.has_value());
        REQUIRE(issues.empty());
        REQUIRE(manifest->menus.size() == 2);
    }
}
