#pragma once

#include "extension_manifest.h"

#include <cstdint>
#include <filesystem>
#include <string>

struct ExternalToolRequest {
    std::string contract_version;
    std::string action_label;
    std::filesystem::path project_root;
    std::filesystem::path selected_path;
    // Caller-selected workspace root. Each run() executes in a fresh unique
    // subdirectory below it, so concurrent or repeated invocations never
    // share request/result files (see ExternalToolRunResult::work_dir).
    std::filesystem::path work_dir;
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
    // Result files larger than this are rejected before JSON parsing.
    static constexpr std::uintmax_t maxResultFileBytes = 1024 * 1024;

    ExternalToolRunResult run(const ExtensionManifest &manifest,
                              const ExternalToolRequest &request) const;
};
