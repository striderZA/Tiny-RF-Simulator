#pragma once

#include "extension_manifest.h"

#include <filesystem>
#include <string>

struct ExternalToolRequest {
    std::string contract_version;
    std::string action_label;
    std::filesystem::path project_root;
    std::filesystem::path selected_path;
    std::filesystem::path work_dir;
    std::filesystem::path result_path;
};

struct ExternalToolRunResult {
    bool ok = false;
    int exit_code = -1;
    std::string message;
    std::filesystem::path result_path;
    std::filesystem::path work_dir;
};

class ExternalToolRunner {
  public:
    ExternalToolRunResult run(const ExtensionManifest &manifest,
                              const ExternalToolRequest &request) const;
};
