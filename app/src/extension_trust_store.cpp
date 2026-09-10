#include "extension_trust_store.h"

#include "logging_core.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kTrustSchemaVersion = 1;

// The persisted row's field spellings, owned here so the reader and the
// writer of the same file cannot drift apart.
constexpr const char *kFieldRoot = "root";
constexpr const char *kFieldId = "id";
constexpr const char *kFieldVersion = "version";
constexpr const char *kFieldEntryPath = "entry_path";

// Directory of the running executable, for exe-relative durable state.
// Mirrors LayoutManager / TutorialState / ExtensionManager; falls back to the
// current working directory if exe-path detection fails.
std::string detectExeDir() {
    std::string exe_path;
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        exe_path = buf;
#elif defined(__APPLE__)
    char buf[PATH_MAX] = {};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        exe_path = buf;
#else
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        exe_path = buf;
    }
#endif
    if (exe_path.empty())
        return fs::current_path().string();
    fs::path parent = fs::path(exe_path).parent_path();
    if (parent.empty())
        return fs::current_path().string();
    return parent.string();
}

bool readString(const json &object, const char *field, std::string &out) {
    if (!object.contains(field) || !object[field].is_string())
        return false;
    out = object[field].get<std::string>();
    return !out.empty();
}

} // namespace

ExtensionTrustStore::ExtensionTrustStore()
    : m_store_path(fs::path(detectExeDir()) / "extension_trust.json") {
    load();
}

ExtensionTrustStore::ExtensionTrustStore(const fs::path &store_path) : m_store_path(store_path) {
    load();
}

void ExtensionTrustStore::setStorePath(const fs::path &store_path) {
    m_store_path = store_path;
    load();
}

std::optional<std::string> ExtensionTrustStore::keyFor(const fs::path &root_dir) {
    if (root_dir.empty())
        return std::nullopt;

    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(root_dir, ec);
    if (ec || canonical.empty())
        return std::nullopt;

    return canonical.generic_string();
}

bool ExtensionTrustStore::isApproved(const fs::path &root_dir) const {
    const auto key = keyFor(root_dir);
    return key && m_entries.count(*key) > 0;
}

std::optional<ExtensionTrustEntry> ExtensionTrustStore::entryFor(const fs::path &root_dir) const {
    const auto key = keyFor(root_dir);
    if (!key)
        return std::nullopt;

    const auto it = m_entries.find(*key);
    return it == m_entries.end() ? std::nullopt : std::optional<ExtensionTrustEntry>(it->second);
}

bool ExtensionTrustStore::approve(const ExtensionManifest &manifest) {
    // A row is only worth persisting if the loader will accept it again; the
    // reader rejects any empty field, so an incomplete manifest must be
    // refused rather than stored as an approval that vanishes on reload.
    if (manifest.id.empty() || manifest.version.empty() || manifest.entry_path.empty()) {
        LOG_WARN("Cannot trust extension '%s': manifest identity or entry point is incomplete",
                 manifest.id.c_str());
        return false;
    }

    const auto key = keyFor(manifest.root_dir);
    if (!key) {
        LOG_WARN("Cannot trust extension '%s': extension root could not be resolved",
                 manifest.id.c_str());
        return false;
    }

    ExtensionTrustEntry entry;
    entry.root = *key;
    entry.id = manifest.id;
    entry.version = manifest.version;
    entry.entry_path = manifest.entry_path.generic_string();

    m_entries[*key] = entry;
    if (!save()) {
        // Memory must describe the file: a failed write means no approval
        // exists, so the run stays refused (fail closed).
        load();
        return false;
    }

    LOG_INFO("Trusted extension '%s' at %s", manifest.id.c_str(), key->c_str());
    return true;
}

bool ExtensionTrustStore::revoke(const fs::path &root_dir) {
    const auto key = keyFor(root_dir);
    if (!key)
        return false;

    const std::size_t removed = m_entries.erase(*key);
    if (removed == 0)
        return true;

    if (!save()) {
        load();
        return false;
    }

    return true;
}

void ExtensionTrustStore::load() {
    m_entries.clear();

    std::error_code ec;
    if (!fs::exists(m_store_path, ec) || ec)
        return;

    std::ifstream in(m_store_path);
    if (!in) {
        LOG_WARN("Extension trust file could not be opened: %s", m_store_path.string().c_str());
        return;
    }

    const json root = json::parse(in, nullptr, false);
    if (root.is_discarded()) {
        LOG_WARN("Extension trust file is not valid JSON: %s", m_store_path.string().c_str());
        return;
    }
    // The comparison must not narrow: get<int> silently wraps, so a hand
    // written schema_version of 4294967297 (2^32 + 1) would land on 1 and be
    // accepted as the current schema. Compare at the widest signed width the
    // JSON integer can occupy and reject anything outside it outright.
    if (!root.is_object() || !root.contains("schema_version") ||
        !root["schema_version"].is_number_integer()) {
        LOG_WARN("Extension trust file has an unsupported schema_version: %s",
                 m_store_path.string().c_str());
        return;
    }
    const std::int64_t declared_schema = root["schema_version"].get<std::int64_t>();
    if (declared_schema != kTrustSchemaVersion) {
        LOG_WARN("Extension trust file has an unsupported schema_version: %s",
                 m_store_path.string().c_str());
        return;
    }
    if (!root.contains("approvals") || !root["approvals"].is_array()) {
        LOG_WARN("Extension trust file has no approvals array: %s", m_store_path.string().c_str());
        return;
    }

    const auto &approvals = root["approvals"];
    for (std::size_t i = 0; i < approvals.size(); ++i) {
        const auto &item = approvals[i];
        if (!item.is_object()) {
            LOG_WARN("Ignoring malformed extension trust entry %zu in %s", i,
                     m_store_path.string().c_str());
            continue;
        }

        ExtensionTrustEntry entry;
        if (!readString(item, kFieldRoot, entry.root) || !readString(item, kFieldId, entry.id) ||
            !readString(item, kFieldVersion, entry.version) ||
            !readString(item, kFieldEntryPath, entry.entry_path)) {
            LOG_WARN("Ignoring malformed extension trust entry %zu in %s", i,
                     m_store_path.string().c_str());
            continue;
        }

        // The key is the canonical root, exactly as every lookup derives it; a
        // hand-edited row (native separators, dot segments) is stored under its
        // canonical spelling so it stays findable and revocable.
        const auto key = keyFor(entry.root);
        if (!key) {
            LOG_WARN("Ignoring extension trust entry %zu with an unresolvable root in %s", i,
                     m_store_path.string().c_str());
            continue;
        }
        entry.root = *key;
        m_entries[*key] = entry;
    }
}

bool ExtensionTrustStore::save() const {
    json approvals = json::array();
    for (const auto &[root, entry] : m_entries) {
        approvals.push_back({{kFieldRoot, entry.root},
                             {kFieldId, entry.id},
                             {kFieldVersion, entry.version},
                             {kFieldEntryPath, entry.entry_path}});
    }

    json out;
    out["schema_version"] = kTrustSchemaVersion;
    out["approvals"] = std::move(approvals);

    std::ofstream file(m_store_path);
    if (!file) {
        LOG_ERROR("Failed to open extension trust file for writing: %s",
                  m_store_path.string().c_str());
        return false;
    }
    file << out.dump(2);
    file.flush();
    if (!file) {
        LOG_ERROR("Failed to write extension trust file: %s", m_store_path.string().c_str());
        return false;
    }
    file.close();
    if (!file) {
        LOG_ERROR("Failed to close extension trust file: %s", m_store_path.string().c_str());
        return false;
    }

    return true;
}
