#pragma once

#include "extension_manifest.h"

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

struct ExtensionRecord {
    ExtensionStatusKind status = ExtensionStatusKind::Invalid;
    std::filesystem::path manifest_path;
    std::vector<ExtensionValidationIssue> issues;
    std::optional<ExtensionManifest> manifest;
};

class ExtensionManager {
  public:
    void rescan(const std::filesystem::path &project_root);

    const std::vector<ExtensionRecord> &all() const { return m_records; }
    std::vector<const ExtensionManifest *> dataPacks() const;
    std::vector<const ExtensionManifest *> externalTools() const;

  private:
    std::vector<std::filesystem::path> scanRoots(const std::filesystem::path &project_root) const;
    void loadRoot(const std::filesystem::path &root);
    std::unordered_map<std::string, std::size_t> m_records_by_id;

    std::vector<ExtensionRecord> m_records;
};
