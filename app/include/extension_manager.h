#pragma once

#include "extension_manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ExtensionRecord {
    ExtensionStatusKind status = ExtensionStatusKind::Invalid;
    std::filesystem::path manifest_path;
    std::vector<ExtensionValidationIssue> issues;
    std::optional<ExtensionManifest> manifest;
    // Non-empty only when status == ExtensionStatusKind::Shadowed: a manifest
    // scanned from an earlier root already provided this id, so this record is
    // excluded from dataPacks()/externalTools() and names the winning file.
    std::string shadow_detail;
};

class ExtensionManager {
  public:
    void rescan(const std::filesystem::path &project_root);

    const std::vector<ExtensionRecord> &all() const { return m_records; }
    std::vector<const ExtensionManifest *> dataPacks() const;
    std::vector<const ExtensionManifest *> externalTools() const;

    // Canonical <project_root>/rf-sim-extensions captured by the last rescan;
    // empty when the project root is unknown or could not be resolved.
    const std::filesystem::path &projectExtensionRoot() const { return m_project_extension_root; }

    // True when the extension was discovered below projectExtensionRoot(),
    // which is the trust boundary: only shipped code lives outside it.
    bool isProjectLocal(const ExtensionRecord &record) const;
    bool isProjectLocal(const ExtensionManifest &manifest) const;

  private:
    std::vector<std::filesystem::path> scanRoots(const std::filesystem::path &project_root) const;
    void loadRoot(const std::filesystem::path &root);
    bool isUnderProjectExtensionRoot(const std::filesystem::path &candidate) const;

    std::unordered_map<std::string, std::size_t> m_records_by_id;

    std::vector<ExtensionRecord> m_records;
    std::filesystem::path m_project_extension_root;
};
