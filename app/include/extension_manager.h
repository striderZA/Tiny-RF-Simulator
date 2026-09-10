#pragma once

#include "extension_manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    // empty only when that path itself is unresolvable. An empty project_root
    // resolves under the current working directory, mirroring how
    // refreshExtensions() treats "no project open" — the CWD's
    // rf-sim-extensions is then the trust boundary, which gates more, never
    // less.
    const std::filesystem::path &projectExtensionRoot() const { return m_project_extension_root; }

    // True when the extension's root is below projectExtensionRoot(), or it
    // was discovered through the project-root scan slot (provenance stamp
    // below). The stamp closes the symlink hole: a linked directory under
    // rf-sim-extensions resolves outside the canonical root, but a project
    // that ships the link ships the trigger, so it is still project-local.
    bool isProjectLocal(const ExtensionManifest &manifest) const;

  private:
    std::vector<std::filesystem::path> scanRoots(const std::filesystem::path &project_root) const;
    void loadRoot(const std::filesystem::path &root, bool from_project_root);
    bool isUnderProjectExtensionRoot(const std::filesystem::path &candidate) const;
    // Canonical generic-string root_dirs of manifests discovered through the
    // project-root scan slot, whatever symlinks resolve them to.
    std::unordered_set<std::string> m_project_local_roots;

    std::unordered_map<std::string, std::size_t> m_records_by_id;

    std::vector<ExtensionRecord> m_records;
    std::filesystem::path m_project_extension_root;
};
