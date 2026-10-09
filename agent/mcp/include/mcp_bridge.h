#pragma once

#include "gui_link.h"

#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>
#include <string>

struct BridgeOptions {
    std::optional<std::filesystem::path> endpoint_file;
    std::filesystem::path exe_dir;
    std::string server_version;
    GuiLinkTimeouts timeouts;
};

int runBridge(std::istream &in, std::ostream &out, std::ostream &err, const BridgeOptions &options);
