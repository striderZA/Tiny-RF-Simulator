#pragma once

#include "gui_link.h"

#include <filesystem>
#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <string>

struct BridgeOptions {
    std::optional<std::filesystem::path> endpoint_file;
    std::filesystem::path exe_dir;
    std::string server_version;
    GuiLinkTimeouts timeouts;
    // Test seam: when set, each tools/call uses it instead of GuiLink. Production leaves it empty.
    std::function<AgentToolResult(const std::string &tool, const nlohmann::ordered_json &arguments)>
        call_override;
};

int runBridge(std::istream &in, std::ostream &out, std::ostream &err, const BridgeOptions &options);
