// Regression coverage for issue #238: test processes that run concurrently must
// not share scratch paths. tests/test_temp_paths.h redirects each process's
// temp directory to a private root. This case pins that the redirect is active,
// so a silent fallback to the shared temp directory fails here instead of
// reappearing as an intermittent collision.
//
// Standalone executable (not part of the main `tests` binary) because the
// MinGW-w64 toolchain silently drops TEST_CASE registrations beyond a ceiling
// in tests.exe; this case must actually run on every CI platform.
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("temp_directory_path is redirected to this process's private root", "[issue238][temp]") {
    const fs::path &root = test_temp_paths::kProcessTempRoot.root();
    REQUIRE_FALSE(root.empty());
    REQUIRE(root.filename().string().find(test_temp_paths::processTag()) != std::string::npos);
    REQUIRE(fs::exists(root));
    REQUIRE(fs::equivalent(fs::temp_directory_path(), root));
}
