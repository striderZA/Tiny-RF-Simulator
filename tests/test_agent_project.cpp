// App-level coverage for the agent project tools. Each case calls RfSimulatorApp::testAgentCall,
// so the real serializer, the GUI save and load paths, the epoch and checkpoint hooks, and the
// unsaved-changes state all run against real .rfsim files in a per-process temp folder.
// Standalone and serial: constructing RfSimulatorApp writes the executable-relative app.ini.
#include "agent_host.h"
#include "app.h"
#include "app_agent_host.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
#include "test_temp_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using nlohmann::ordered_json;

struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        ImGui::GetIO().DisplaySize = ImVec2(1200.0f, 800.0f);
        ImGui::GetIO().IniFilename = nullptr;
        unsigned char *pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};

struct ProjectFixture : ImGuiFixture {
    RfSimulatorApp app;
    fs::path temp_dir =
        fs::temp_directory_path() / ("agent_project_" + test_temp_paths::processTag());

    ProjectFixture() {
        fs::create_directories(temp_dir);
        app.newProject();
    }

    ~ProjectFixture() {
        std::error_code error;
        fs::remove_all(temp_dir, error);
    }

    fs::path file(const std::string &name) const { return temp_dir / name; }

    AgentToolResult call(const std::string &tool, ordered_json arguments = ordered_json::object()) {
        return app.testAgentCall(tool, std::move(arguments));
    }

    // Builds the open circuit from |types| with fixed engine ids, as the GUI would place them.
    void buildCircuit(std::initializer_list<const char *> types) {
        int engine_id = 100;
        for (const char *type : types) {
            REQUIRE(app.testCreateComponent(type, engine_id++) != nullptr);
        }
    }
};

std::string readText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeText(const fs::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE_METHOD(ProjectFixture, "project_save writes the open circuit to a .rfsim file",
                 "[agent_app][project]") {
    buildCircuit({"generator"});
    REQUIRE(app.isDirty());
    const fs::path target = file("first.rfsim");

    const AgentToolResult result = call("project_save", {{"path", target.string()}});
    REQUIRE_FALSE(result.is_error);
    CHECK(result.structured.at("name") == "first");
    CHECK(result.structured.at("epoch") == app.testProjectEpoch());
    CHECK_FALSE(app.isDirty());
    CHECK(app.m_current_project_path == target.lexically_normal().string());
    CHECK_FALSE(fs::exists(target.string() + ".tmp"));

    const auto saved = nlohmann::json::parse(readText(target));
    CHECK(saved.at("name") == "first");
    CHECK(saved.at("components").size() == 1);
    CHECK(result.structured.at("copied_data_files").empty());
}

TEST_CASE_METHOD(ProjectFixture, "project_save reports external RF data copies",
                 "[agent_app][project]") {
    const fs::path external =
        temp_dir.parent_path() / (temp_dir.filename().string() + "_external.s2p");
    {
        std::ofstream out(external, std::ios::binary);
        out << "# GHz S MA R 50\n"
               "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";
    }
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 100));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.string());
    REQUIRE(amp->sparamLoaded());

    const fs::path target = file("external.rfsim");
    const AgentToolResult result = call("project_save", {{"path", target.string()}});
    REQUIRE_FALSE(result.is_error);
    REQUIRE(result.structured.contains("copied_data_files"));
    REQUIRE(result.structured["copied_data_files"].size() == 1);
    const fs::path copied = temp_dir / result.structured["copied_data_files"][0].get<std::string>();
    CHECK(fs::is_regular_file(copied));
    CHECK(readText(copied) == readText(external));
    const AgentToolResult reused = call("project_save");
    REQUIRE_FALSE(reused.is_error);
    CHECK(reused.structured.at("copied_data_files").empty());
    fs::remove(external);
}
TEST_CASE_METHOD(ProjectFixture, "project_save reports external data copy failures as INTERNAL",
                 "[agent_app][project][failure]") {
    const fs::path external =
        temp_dir.parent_path() / (temp_dir.filename().string() + "_missing.s2p");
    {
        std::ofstream out(external, std::ios::binary);
        out << "# GHz S MA R 50\n"
               "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";
    }
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 101));
    REQUIRE(amp != nullptr);
    amp->setSParamFilepath(external.string());
    REQUIRE(amp->sparamLoaded());
    fs::remove(external);

    const fs::path target = file("existing.rfsim");
    const std::string previous = R"({"previous":"intact"})";
    writeText(target, previous);
    const AgentToolResult result =
        call("project_save", {{"path", target.string()}, {"overwrite", true}});
    REQUIRE(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INTERNAL");
    CHECK(readText(target) == previous);
}

TEST_CASE_METHOD(ProjectFixture,
                 "project_load replaces the open circuit and clears agent checkpoints",
                 "[agent_app][project]") {
    buildCircuit({"generator", "amplifier"});
    const fs::path two = file("two.rfsim");
    REQUIRE_FALSE(call("project_save", {{"path", two.string()}}).is_error);

    app.newProject();
    buildCircuit({"generator"});
    const fs::path one = file("one.rfsim");
    REQUIRE_FALSE(call("project_save", {{"path", one.string()}}).is_error);

    // An agent edit leaves a checkpoint and an unsaved change; saving clears the change.
    const ordered_json add_attenuator{{"op", "add"}, {"type", "attenuator"}};
    const AgentToolResult edit =
        call("circuit_edit",
             {{"epoch", app.testProjectEpoch()}, {"ops", ordered_json::array({add_attenuator})}});
    REQUIRE_FALSE(edit.is_error);
    REQUIRE_FALSE(app.testAgentHost().checkpoints().empty());
    REQUIRE_FALSE(call("project_save").is_error);
    REQUIRE_FALSE(app.isDirty());

    const std::uint64_t before = app.testProjectEpoch();
    const AgentToolResult loaded =
        call("project_load", {{"epoch", before}, {"path", two.string()}});
    REQUIRE_FALSE(loaded.is_error);
    CHECK(app.componentCount() == 2);
    CHECK(app.testProjectEpoch() != before);
    CHECK(loaded.structured.at("epoch") == app.testProjectEpoch());
    CHECK(app.testAgentHost().checkpoints().empty());
    CHECK_FALSE(app.isDirty());
    CHECK(app.m_current_project_path == two.lexically_normal().string());

    // The replacement is announced to agents: the old epoch is stale and names the cause.
    const AgentToolResult stale =
        call("circuit_edit", {{"epoch", before}, {"ops", ordered_json::array({add_attenuator})}});
    REQUIRE(stale.is_error);
    CHECK(stale.structured.at("error").at("code") == "STALE_EPOCH");
    CHECK(stale.structured.at("error").at("details").at("cause") == "opened_project");
}

TEST_CASE_METHOD(ProjectFixture, "project_load refuses unsaved changes and leaves the file alone",
                 "[agent_app][project]") {
    buildCircuit({"generator"});
    const fs::path one = file("one.rfsim");
    REQUIRE_FALSE(call("project_save", {{"path", one.string()}}).is_error);
    const std::string saved_bytes = readText(one);
    const std::uint64_t epoch = app.testProjectEpoch();
    const std::size_t count = app.componentCount();

    writeText(file("other.rfsim"), "{}");
    app.testMakeDirty();
    const AgentToolResult result =
        call("project_load", {{"epoch", epoch}, {"path", file("other.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(result.structured.at("error").at("code") == "UNSAVED_CHANGES");
    CHECK(app.componentCount() == count);
    CHECK(app.testProjectEpoch() == epoch);
    CHECK(app.m_current_project_path == one.lexically_normal().string());
    CHECK(app.isDirty());
    CHECK(readText(one) == saved_bytes);
}

TEST_CASE_METHOD(ProjectFixture,
                 "project_save refuses to replace another project without overwrite",
                 "[agent_app][project]") {
    buildCircuit({"generator"});
    const fs::path other = file("other.rfsim");
    REQUIRE_FALSE(call("project_save", {{"path", other.string()}}).is_error);
    const std::string other_bytes = readText(other);

    app.newProject();
    buildCircuit({"amplifier"});
    const AgentToolResult refused = call("project_save", {{"path", other.string()}});
    REQUIRE(refused.is_error);
    CHECK(refused.structured.at("error").at("code") == "INVALID_ARGUMENT");
    CHECK(refused.structured.at("error").at("details").at("path") == "/overwrite");
    CHECK(readText(other) == other_bytes);
    CHECK(app.isDirty());
    CHECK(app.m_current_project_path.empty());

    const AgentToolResult replaced =
        call("project_save", {{"path", other.string()}, {"overwrite", true}});
    REQUIRE_FALSE(replaced.is_error);
    CHECK(readText(other) != other_bytes);
    CHECK_FALSE(app.isDirty());
}

TEST_CASE_METHOD(ProjectFixture, "project_save of an untitled project needs an explicit path",
                 "[agent_app][project]") {
    buildCircuit({"generator"});

    const AgentToolResult result = call("project_save");
    REQUIRE(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    CHECK(result.structured.at("error").at("details").at("path") == "/path");
    CHECK(app.isDirty());
    CHECK(app.m_current_project_path.empty());
}

TEST_CASE_METHOD(ProjectFixture, "a project file that cannot be parsed leaves the circuit intact",
                 "[agent_app][project]") {
    buildCircuit({"generator"});
    const fs::path one = file("one.rfsim");
    REQUIRE_FALSE(call("project_save", {{"path", one.string()}}).is_error);
    const std::uint64_t epoch = app.testProjectEpoch();
    writeText(file("truncated.rfsim"), "{");

    const AgentToolResult result =
        call("project_load", {{"epoch", epoch}, {"path", file("truncated.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    CHECK(app.componentCount() == 1);
    CHECK(app.testProjectEpoch() == epoch);
    CHECK(app.m_current_project_path == one.lexically_normal().string());
    CHECK_FALSE(app.isDirty());
}

TEST_CASE_METHOD(ProjectFixture, "a restore failure clears the open project and reports INTERNAL",
                 "[agent_app][project]") {
    buildCircuit({"generator"});
    REQUIRE_FALSE(call("project_save", {{"path", file("one.rfsim").string()}}).is_error);
    const std::uint64_t epoch = app.testProjectEpoch();
    // A wrong-shaped section fails restoration after the live project has been reset.
    writeText(file("broken.rfsim"), R"({"components": 5})");

    const AgentToolResult result =
        call("project_load", {{"epoch", epoch}, {"path", file("broken.rfsim").string()}});
    REQUIRE(result.is_error);
    CHECK(result.structured.at("error").at("code") == "INTERNAL");
    CHECK(app.componentCount() == 0);
    CHECK(app.testProjectEpoch() != epoch);
    CHECK(result.structured.at("epoch") == app.testProjectEpoch());
    CHECK(app.m_current_project_path.empty());
    CHECK(app.isDirty());
}

TEST_CASE_METHOD(ProjectFixture, "project tools refuse paths that name another file",
                 "[agent_app][project]") {
    buildCircuit({"generator"});
    const fs::path victim = file("victim.txt");
    writeText(victim, "keep me");
    // The C runtime and Win32 stop reading a path at a NUL, so this text names victim.txt.
    const std::string nul_path = victim.string() + std::string("\0.rfsim", 7);

    SECTION("saving over a NUL-suffixed path leaves the other file unchanged") {
        const AgentToolResult result =
            call("project_save", {{"path", nul_path}, {"overwrite", true}});
        CHECK(readText(victim) == "keep me");
        REQUIRE(result.is_error);
        CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    }
    SECTION("saving to a NUL-suffixed name creates no file with that name") {
        const fs::path ghost = file("ghost.txt");
        const AgentToolResult result =
            call("project_save", {{"path", ghost.string() + std::string("\0.rfsim", 7)}});
        CHECK_FALSE(fs::exists(ghost));
        REQUIRE(result.is_error);
        CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    }
    SECTION("loading a NUL-suffixed path does not read the other file") {
        // victim.txt holds a valid project, so a truncated load would replace the open circuit.
        const fs::path project = file("one.rfsim");
        REQUIRE_FALSE(call("project_save", {{"path", project.string()}}).is_error);
        writeText(victim, readText(project));
        const std::uint64_t epoch = app.testProjectEpoch();

        const AgentToolResult result = call("project_load", {{"epoch", epoch}, {"path", nul_path}});
        CHECK(app.testProjectEpoch() == epoch);
        REQUIRE(result.is_error);
        CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    }
#ifdef _WIN32
    SECTION("saving to a stream name creates no file") {
        const fs::path base = file("stream.txt");
        const AgentToolResult result = call("project_save", {{"path", base.string() + ":s.rfsim"}});
        CHECK_FALSE(fs::exists(base));
        REQUIRE(result.is_error);
        CHECK(result.structured.at("error").at("code") == "INVALID_ARGUMENT");
    }
#endif
}
