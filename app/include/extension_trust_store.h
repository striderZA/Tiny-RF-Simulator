#pragma once

#include "extension_manifest.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

// One approval decision: the extension root the user trusted, plus the
// identity the manifest had at the moment it was approved (shown by the
// Extensions panel when the entry point has since changed).
struct ExtensionTrustEntry {
    std::string root;
    std::string id;
    std::string version;
    std::string entry_path;
};

// Persisted record of which extension directories the user allowed to execute.
// Approvals are keyed by the canonical extension root (VS Code workspace-trust
// shape): editing files inside an approved folder does not re-prompt, a copy at
// a different path needs its own approval.
//
// Every degraded path (missing file, unreadable, wrong schema, malformed
// entry) resolves to "not approved" and is logged; nothing is ever trusted by
// default and no exception escapes.
class ExtensionTrustStore {
  public:
    ExtensionTrustStore();
    explicit ExtensionTrustStore(const std::filesystem::path &store_path);

    // Re-points the store at another file and reloads it. Tests use it to keep
    // their approvals out of the real <exe_dir>/extension_trust.json.
    void setStorePath(const std::filesystem::path &store_path);

    const std::filesystem::path &storePath() const { return m_store_path; }

    bool isApproved(const std::filesystem::path &root_dir) const;
    std::optional<ExtensionTrustEntry> entryFor(const std::filesystem::path &root_dir) const;

    // Both persist immediately and return false when the file could not be
    // written; a failed approve() leaves the root unapproved (fail closed).
    bool approve(const ExtensionManifest &manifest);
    bool revoke(const std::filesystem::path &root_dir);

  private:
    static std::optional<std::string> keyFor(const std::filesystem::path &root_dir);

    void load();
    bool save() const;

    std::filesystem::path m_store_path;
    std::map<std::string, ExtensionTrustEntry> m_entries;
};
