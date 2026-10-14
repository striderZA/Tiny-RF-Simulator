#if __has_include("imgui.h") || __has_include("imnodes.h")
#error "agent_api tests must remain independent of the UI"
#endif

// Contract tests for the project_save and project_load tools: path and overwrite rules, the
// unsaved-changes and epoch guards, the test-flow latch, and how host outcomes map to protocol
// errors and epochs. Files are real and live in a per-process temp folder; the host is the fake
// from agent_api_test_support.h. The GUI path is covered by test_agent_project.
#include "agent_api_test_support.h"

#include "test_temp_paths.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

// A scratch folder unique to this process and test, removed when the test ends.
struct ProjectTempDir {
    fs::path dir;

    ProjectTempDir() {
        static int counter = 0;
        dir = fs::temp_directory_path() / ("agent_api_project_" + test_temp_paths::processTag() +
                                           "_" + std::to_string(++counter));
        std::error_code ec;
        fs::create_directories(dir, ec);
    }

    ~ProjectTempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    fs::path file(const std::string &name) const { return dir / name; }
};

void writeFile(const fs::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE("project_save rejects paths that are not absolute .rfsim files", "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const std::string json_file = temp.file("notes.json").string();

    for (const std::string path :
         {std::string{"relative/project.rfsim"}, json_file, std::string{}}) {
        CAPTURE(path);
        const auto result = fixture.call("project_save", {{"path", path}});
        REQUIRE(result.is_error);
        CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
        CHECK(errorFor(result).at("details").at("path") == "/path");
    }
    CHECK(fixture.host.saved_paths.empty());
}

TEST_CASE("project_save without a path needs an open project file", "[agent_api][project]") {
    ApiFixture fixture;

    const auto result = fixture.call("project_save");
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(result).at("details").at("path") == "/path");
    CHECK(fixture.host.saved_paths.empty());
}

TEST_CASE("project_save writes the given file and reports it without editing the circuit",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    REQUIRE(fixture.add<SignalGeneratorEngine>("generator") != nullptr);
    const std::uint64_t epoch = fixture.api.epoch();
    const std::uint64_t revision = fixture.commands.revision();
    const fs::path target = temp.file("amp.rfsim");
    const std::string normal = target.lexically_normal().string();

    const auto result = fixture.call("project_save", {{"path", target.string()}});
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("epoch") == epoch);
    CHECK(result.structured.at("path") == normal);
    CHECK(result.structured.at("name") == "amp");
    REQUIRE(fixture.host.saved_paths.size() == 1);
    CHECK(fixture.host.saved_paths.front() == normal);
    CHECK(fixture.host.open_path == normal);

    // Saving is not an edit: no checkpoint, no revision change, and the epoch stays put.
    CHECK(fixture.commands.revision() == revision);
    CHECK(fixture.host.checkpoint_begins == 0);
    CHECK(fixture.api.epoch() == epoch);
    REQUIRE_FALSE(fixture.host.activities.empty());
    CHECK(fixture.host.activities.back().tool == "project_save");
    CHECK(fixture.host.activities.back().summary == "Save project");
}

TEST_CASE("project_save replaces a different existing file only with overwrite true",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const fs::path other = temp.file("other.rfsim");
    writeFile(other, "keep me");

    const auto refused = fixture.call("project_save", {{"path", other.string()}});
    REQUIRE(refused.is_error);
    CHECK(errorFor(refused).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(refused).at("details").at("path") == "/overwrite");
    CHECK(fixture.host.saved_paths.empty());

    const auto replaced =
        fixture.call("project_save", {{"path", other.string()}, {"overwrite", true}});
    REQUIRE_FALSE(replaced.is_error);
    CHECK(fixture.host.saved_paths.size() == 1);
}

TEST_CASE("project_save to the open project's own file needs no overwrite flag",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const fs::path open = temp.file("open.rfsim");
    writeFile(open, "{}");
    fixture.host.open_path = open.string();

    const auto result = fixture.call("project_save");
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("path") == open.lexically_normal().string());
    CHECK(fixture.host.saved_paths.size() == 1);
}

TEST_CASE("project_save reports a missing folder or a folder path as INVALID_ARGUMENT",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const fs::path missing_folder = temp.file("no_such_folder") / "project.rfsim";
    const fs::path folder_named = temp.file("folder.rfsim");
    fs::create_directories(folder_named);

    for (const fs::path &path : {missing_folder, folder_named}) {
        CAPTURE(path.string());
        const auto result = fixture.call("project_save", {{"path", path.string()}});
        REQUIRE(result.is_error);
        CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
        CHECK(errorFor(result).at("details").at("path") == "/path");
    }
    CHECK(fixture.host.saved_paths.empty());
}

TEST_CASE("project_save reports a failed write as INTERNAL and keeps the open file",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    fixture.host.save_succeeds = false;

    const auto result = fixture.call("project_save", {{"path", temp.file("fails.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INTERNAL");
    CHECK_FALSE(fixture.host.open_path.has_value());
}

TEST_CASE("project_save is refused while a failed test-flow restore has latched",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    auto *generator = fixture.add<SignalGeneratorEngine>("generator");
    REQUIRE(generator != nullptr);
    fixture.api.setFlowRunBoundary(
        [](const FlowSpec &, std::span<IComponentEngine *const>, const NodeGraphEngine &) {
            BoundaryOutcome outcome;
            outcome.status = BoundaryStatus::RestoreFailed;
            outcome.message = "component 3: intentional";
            return outcome;
        });
    const auto run = fixture.call(
        "test_flow_run",
        {{"epoch", fixture.api.epoch()},
         {"measure", nlohmann::ordered_json::array({nlohmann::ordered_json{
                         {"component", generator->id()}, {"port", 0}, {"metric", "power_dBm"}}})}});
    REQUIRE(errorFor(run).at("code") == "INTERNAL");

    const std::string target = temp.file("latched.rfsim").string();
    const auto refused = fixture.call("project_save", {{"path", target}});
    REQUIRE(refused.is_error);
    CHECK(errorFor(refused).at("code") == "INTERNAL");
    CHECK(errorFor(refused).at("message").get<std::string>().find("project not saved") !=
          std::string::npos);
    CHECK(fixture.host.saved_paths.empty());

    // The restore failure also marks the project unsaved, so project_load is refused by that flag.
    // Its hint must not tell the caller to save first, since the latch refuses project_save.
    const std::string latched_in = temp.file("latched-in.rfsim").string();
    writeFile(latched_in, "{}");
    const auto load =
        fixture.call("project_load", {{"epoch", fixture.api.epoch()}, {"path", latched_in}});
    REQUIRE(load.is_error);
    CHECK(errorFor(load).at("code") == "UNSAVED_CHANGES");
    const std::string hint = errorFor(load).at("hint").get<std::string>();
    CHECK(hint.find("save with project_save first") == std::string::npos);
    CHECK(hint.find("ask the user") != std::string::npos);
    CHECK(fixture.host.loaded_paths.empty());

    fixture.api.noteProjectReplaced(AgentReplacementCause::NewProject);
    const auto after = fixture.call("project_save", {{"path", target}});
    REQUIRE_FALSE(after.is_error);
    CHECK(fixture.host.saved_paths.size() == 1);
}

TEST_CASE("project_load needs the current epoch and an absolute .rfsim path",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const std::string path = temp.file("in.rfsim").string();
    writeFile(temp.file("in.rfsim"), "{}");

    const auto no_epoch = fixture.call("project_load", {{"path", path}});
    REQUIRE(no_epoch.is_error);
    CHECK(errorFor(no_epoch).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(no_epoch).at("details").at("path") == "/epoch");

    const auto no_path = fixture.call("project_load", {{"epoch", fixture.api.epoch()}});
    REQUIRE(no_path.is_error);
    CHECK(errorFor(no_path).at("details").at("path") == "/path");

    const auto relative =
        fixture.call("project_load", {{"epoch", fixture.api.epoch()}, {"path", "in.rfsim"}});
    REQUIRE(relative.is_error);
    CHECK(errorFor(relative).at("code") == "INVALID_ARGUMENT");
    CHECK(fixture.host.loaded_paths.empty());
}

TEST_CASE("project_load refuses a stale epoch before reading the file", "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    writeFile(temp.file("in.rfsim"), "{}");

    const auto result = fixture.call("project_load", {{"epoch", fixture.api.epoch() + 1},
                                                      {"path", temp.file("in.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "STALE_EPOCH");
    CHECK(fixture.host.loaded_paths.empty());
}

TEST_CASE("project_load reports a missing file as NOT_FOUND and a folder as INVALID_ARGUMENT",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const fs::path folder = temp.file("folder.rfsim");
    fs::create_directories(folder);

    const auto missing =
        fixture.call("project_load", {{"epoch", fixture.api.epoch()},
                                      {"path", temp.file("absent.rfsim").string()}});
    REQUIRE(missing.is_error);
    CHECK(errorFor(missing).at("code") == "NOT_FOUND");

    const auto is_folder =
        fixture.call("project_load", {{"epoch", fixture.api.epoch()}, {"path", folder.string()}});
    REQUIRE(is_folder.is_error);
    CHECK(errorFor(is_folder).at("code") == "INVALID_ARGUMENT");
    CHECK(fixture.host.loaded_paths.empty());
}

TEST_CASE("project_load refuses unsaved changes without calling the host", "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    writeFile(temp.file("in.rfsim"), "{}");
    REQUIRE(fixture.add<SignalGeneratorEngine>("generator") != nullptr);
    REQUIRE(fixture.commands.isDirty());
    const std::uint64_t epoch = fixture.api.epoch();

    const auto result =
        fixture.call("project_load", {{"epoch", epoch}, {"path", temp.file("in.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "UNSAVED_CHANGES");
    CHECK(fixture.host.loaded_paths.empty());
    CHECK(fixture.api.epoch() == epoch);
    CHECK(fixture.commands.isDirty());
}

TEST_CASE("project_load reports the epoch after the replacement and starts clean",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    writeFile(temp.file("next.rfsim"), "{}");
    // Stands in for the GUI load: a successful restore replaces the circuit and marks it clean.
    fixture.host.load_outcome = [&fixture](const std::string &) {
        fixture.runtime.clearComponentsAndResetIds();
        fixture.commands.markClean();
        return AgentProjectLoadResult{true, false};
    };
    const std::uint64_t before = fixture.api.epoch();

    const auto result = fixture.call(
        "project_load", {{"epoch", before}, {"path", temp.file("next.rfsim").string()}});
    REQUIRE_FALSE(result.is_error);
    CHECK(fixture.api.epoch() == before + 1);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    CHECK(result.structured.at("name") == "next");
    CHECK(fixture.host.checkpoint_begins == 0);
    CHECK_FALSE(fixture.commands.isDirty());
    REQUIRE(fixture.host.loaded_paths.size() == 1);
}

TEST_CASE("project_load reports an unreadable file as INVALID_ARGUMENT and keeps the epoch",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    writeFile(temp.file("bad.rfsim"), "{");
    fixture.host.load_outcome = [](const std::string &) {
        return AgentProjectLoadResult{false, false};
    };
    const std::uint64_t epoch = fixture.api.epoch();

    const auto result =
        fixture.call("project_load", {{"epoch", epoch}, {"path", temp.file("bad.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INVALID_ARGUMENT");
    CHECK(errorFor(result).at("details").at("path") == "/path");
    CHECK(result.structured.at("epoch") == epoch);
    CHECK(fixture.api.epoch() == epoch);
    REQUIRE(fixture.host.loaded_paths.size() == 1);
}

TEST_CASE("project_load reports a restore that cleared the circuit as INTERNAL at the new epoch",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    writeFile(temp.file("broken.rfsim"), R"({"components": 5})");
    fixture.host.load_outcome = [&fixture](const std::string &) {
        fixture.runtime.clearComponentsAndResetIds();
        fixture.commands.markModified();
        return AgentProjectLoadResult{false, true};
    };
    const std::uint64_t before = fixture.api.epoch();

    const auto result = fixture.call(
        "project_load", {{"epoch", before}, {"path", temp.file("broken.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(errorFor(result).at("code") == "INTERNAL");
    CHECK(errorFor(result).at("message").get<std::string>().find("cleared") != std::string::npos);
    CHECK(result.structured.at("epoch") == fixture.api.epoch());
    CHECK(fixture.api.epoch() == before + 1);
}

TEST_CASE("project tools reject unknown or mistyped arguments", "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const std::string path = temp.file("typed.rfsim").string();

    const auto unknown = fixture.call("project_save", {{"path", path}, {"format", "json"}});
    REQUIRE(unknown.is_error);
    CHECK(errorFor(unknown).at("code") == "INVALID_ARGUMENT");

    const auto mistyped = fixture.call("project_save", {{"path", path}, {"overwrite", "yes"}});
    REQUIRE(mistyped.is_error);
    CHECK(errorFor(mistyped).at("details").at("path") == "/overwrite");
    CHECK(fixture.host.saved_paths.empty());
}

TEST_CASE("project tools refuse paths that the OS would open under another name",
          "[agent_api][project]") {
    ApiFixture fixture;
    ProjectTempDir temp;
    const std::string victim = temp.file("notes.txt").string();
    const auto refused = [&fixture](const std::string &path) {
        const auto saved = fixture.call("project_save", {{"path", path}});
        REQUIRE(saved.is_error);
        CHECK(errorFor(saved).at("code") == "INVALID_ARGUMENT");
        CHECK(errorFor(saved).at("details").at("path") == "/path");

        const auto loaded =
            fixture.call("project_load", {{"epoch", fixture.api.epoch()}, {"path", path}});
        REQUIRE(loaded.is_error);
        CHECK(errorFor(loaded).at("code") == "INVALID_ARGUMENT");
        CHECK(errorFor(loaded).at("details").at("path") == "/path");
    };
    // The C runtime and Win32 stop reading a path at a NUL, so this text opens notes.txt.
    refused(victim + std::string("\0.rfsim", 7));
#ifdef _WIN32
    // NTFS reads "name:stream" as an alternate data stream of "name".
    refused(victim + ":s.rfsim");
#endif
    CHECK(fixture.host.saved_paths.empty());
    CHECK(fixture.host.loaded_paths.empty());
}
