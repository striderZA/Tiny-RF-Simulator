#include "agent_args.h"
#include "agent_tools.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

[[noreturn]] void fail(AgentError error) { throw AgentArgumentError(std::move(error)); }

[[noreturn]] void invalid(std::string path, std::string message,
                          std::optional<std::string> hint = std::nullopt) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message), std::move(hint)};
    error.details = {{"path", std::move(path)}};
    fail(std::move(error));
}

// Agent project files are absolute and end in .rfsim. A relative path would resolve against the
// GUI's working directory, and the extension keeps agent writes to project files.
fs::path projectFile(const std::string &text, const std::string &hint) {
    const fs::path path(text);
    if (text.empty() || !path.is_absolute())
        invalid("/path", "expected an absolute path to a .rfsim file", hint);
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".rfsim")
        invalid("/path", "expected a .rfsim file", hint);
    return path.lexically_normal();
}

// True when |target| is the file the GUI would write on Save. The GUI's Save never prompts,
// so agent saves to this file need no overwrite flag.
bool isOpenProjectFile(const fs::path &target, const std::optional<std::string> &open_file) {
    std::error_code ec;
    return open_file && fs::equivalent(target, *open_file, ec);
}

AgentToolResult projectResult(std::uint64_t epoch, const fs::path &path) {
    AgentToolResult result;
    result.structured = {{"epoch", epoch}, {"path", path.string()}, {"name", path.stem().string()}};
    return result;
}

} // namespace

AgentToolResult executeProjectSaveTool(const AgentApi &api, const AgentCall &call) {
    const AgentApiContext &context = api.m_context;
    AgentArgs args(call.arguments, {"path", "overwrite"});
    const bool overwrite = args.optionalBool("overwrite", false);
    const std::optional<std::string> open_file = context.host.projectPath();

    fs::path target;
    if (call.arguments.contains("path")) {
        const auto &value = call.arguments.at("path");
        if (!value.is_string())
            invalid("/path", "expected a string");
        target = projectFile(value.get<std::string>(), "give the full path, such as "
                                                       "C:/Projects/name.rfsim");
    } else {
        if (!open_file)
            invalid("/path", "the project has no file yet", "pass path to choose where to save");
        target = projectFile(*open_file, "the open project's file is not a .rfsim file; pass path");
    }

    // A failed test-flow restore can leave swept values in the circuit. Saving them would persist
    // that state, so the save is refused until the user reloads the project.
    const std::uint64_t epoch = api.epoch();
    if (!api.m_flow_latch_message.empty()) {
        return agentErrorResult(AgentError{AgentErrorCode::Internal,
                                           "project not saved; " + api.m_flow_latch_message,
                                           "reload the project in RF Simulator before saving"},
                                epoch);
    }

    std::error_code ec;
    if (!fs::is_directory(target.parent_path(), ec))
        invalid("/path", "the folder does not exist", "create the folder or choose another path");
    if (fs::is_directory(target, ec))
        invalid("/path", "the path is a folder", "give a file name ending in .rfsim");
    const bool replaces_file = fs::exists(target, ec);
    if (replaces_file && !overwrite && !isOpenProjectFile(target, open_file))
        invalid("/overwrite", "a file already exists at this path",
                "pass overwrite true to replace it, or choose another path");

    if (!context.host.saveProjectFile(target.string())) {
        return agentErrorResult(AgentError{AgentErrorCode::Internal,
                                           "could not write the project file",
                                           "see the RF Simulator log"},
                                epoch);
    }
    return projectResult(api.epoch(), target);
}

AgentToolResult executeProjectLoadTool(const AgentApi &api, const AgentCall &call) {
    const AgentApiContext &context = api.m_context;
    AgentArgs args(call.arguments, {"epoch", "path"});
    const std::uint64_t sent_epoch = args.requiredUInt64("epoch");
    if (!call.arguments.contains("path"))
        invalid("/path", "required field is missing");
    const auto &value = call.arguments.at("path");
    if (!value.is_string())
        invalid("/path", "expected a string");
    const fs::path source =
        projectFile(value.get<std::string>(), "give the full path of an existing .rfsim file");

    const std::uint64_t epoch = api.epoch();
    if (sent_epoch != epoch)
        return staleMeasurementEpochError(epoch, api.m_last_replacement_cause,
                                          api.m_undone_summaries);

    std::error_code ec;
    if (!fs::exists(source, ec))
        fail(AgentError{AgentErrorCode::NotFound, "project file is missing on disk"});
    if (!fs::is_regular_file(source, ec))
        invalid("/path", "expected a project file, not a folder");

    // Loading replaces the open circuit and its checkpoints, so unsaved edits are never discarded.
    if (context.commands.isDirty()) {
        // A test-flow latch also marks the project unsaved, and it refuses project_save, so the
        // hint must not point back to project_save.
        const std::string hint =
            api.m_flow_latch_message.empty()
                ? "save with project_save first, or ask the user to save or discard the changes "
                  "in RF Simulator"
                : "project_save is refused after a failed test-flow restore; ask the user to save "
                  "or discard the changes in RF Simulator";
        fail(AgentError{AgentErrorCode::UnsavedChanges, "the open project has unsaved changes",
                        hint});
    }

    const AgentProjectLoadResult loaded = context.host.loadProjectFile(source.string());
    if (!loaded.loaded) {
        if (loaded.cleared) {
            return agentErrorResult(
                AgentError{AgentErrorCode::Internal,
                           "the project file could not be restored, so the open circuit was "
                           "cleared",
                           "call circuit_get; see the RF Simulator log"},
                api.epoch());
        }
        AgentError error{AgentErrorCode::InvalidArgument,
                         "the file could not be loaded as a project",
                         "check that the file is a valid RF Simulator project"};
        error.details = {{"path", "/path"}};
        return agentErrorResult(error, api.epoch());
    }
    return projectResult(api.epoch(), source);
}
