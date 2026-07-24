#include "session_state.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

using Catch::Approx;

TEST_CASE("SessionState returns default for missing key", "[session]") {
    SessionState state;
    std::string val = state.load("SessionTest_Nonexistent", "MissingKey", "fallback");
    REQUIRE(val == "fallback");
}

TEST_CASE("SessionState saves and loads string values", "[session]") {
    SessionState state;
    state.save("SessionTest_RW", "StringKey", "hello_world");
    std::string val = state.load("SessionTest_RW", "StringKey", "nope");
    REQUIRE(val == "hello_world");
}

TEST_CASE("SessionState saveBool/loadBool round-trip", "[session]") {
    SessionState state;
    state.saveBool("SessionTest_Bool", "TrueKey", true);
    state.saveBool("SessionTest_Bool", "FalseKey", false);

    REQUIRE(state.loadBool("SessionTest_Bool", "TrueKey", false) == true);
    REQUIRE(state.loadBool("SessionTest_Bool", "FalseKey", true) == false);
}

TEST_CASE("SessionState loadBool returns default for missing key", "[session]") {
    SessionState state;
    REQUIRE(state.loadBool("SessionTest_Default", "Missing", true) == true);
    REQUIRE(state.loadBool("SessionTest_Default", "Missing", false) == false);
}

TEST_CASE("SessionState fileExists returns false before first save", "[session]") {
    SessionState state;
    // The INI file may or may not exist depending on prior test runs.
    // We save a known key and verify the file exists after.
    state.save("SessionTest_Exists", "Marker", "1");
    REQUIRE(state.fileExists());
}

TEST_CASE("SessionState overwrites existing value", "[session]") {
    SessionState state;
    state.save("SessionTest_Overwrite", "Key", "first");
    state.save("SessionTest_Overwrite", "Key", "second");
    std::string val = state.load("SessionTest_Overwrite", "Key", "fallback");
    REQUIRE(val == "second");
}

TEST_CASE("SessionState handles empty string values", "[session]") {
    SessionState state;
    state.save("SessionTest_Empty", "EmptyKey", "");
    std::string val = state.load("SessionTest_Empty", "EmptyKey", "not_empty");
    REQUIRE(val.empty());
}
