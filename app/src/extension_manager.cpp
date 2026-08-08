#include "extension_manager.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

std::optional<int> parseVersionPart(std::string_view part) {
    if (part.empty())
        return std::nullopt;

    int value = 0;
    const auto *begin = part.data();
    const auto *end = part.data() + part.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end)
        return std::nullopt;
    return value;
}

std::optional<std::vector<int>> parseVersion(std::string_view version) {
    if (version.empty())
        return std::nullopt;

    std::vector<int> parts;
    std::size_t start = 0;
    while (start <= version.size()) {
        const std::size_t dot = version.find('.', start);
        const std::size_t count =
            dot == std::string_view::npos ? version.size() - start : dot - start;
        const auto part = parseVersionPart(version.substr(start, count));
        if (!part)
            return std::nullopt;
        parts.push_back(*part);
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
        if (start == version.size())
            return std::nullopt;
    }
    return parts;
}

std::optional<int> compareVersions(std::string_view lhs, std::string_view rhs) {
    const auto lhs_parts = parseVersion(lhs);
    const auto rhs_parts = parseVersion(rhs);
    if (!lhs_parts || !rhs_parts)
        return std::nullopt;

    const std::size_t count = std::max(lhs_parts->size(), rhs_parts->size());
    for (std::size_t i = 0; i < count; ++i) {
        const int a = i < lhs_parts->size() ? (*lhs_parts)[i] : 0;
        const int b = i < rhs_parts->size() ? (*rhs_parts)[i] : 0;
        if (a < b)
            return -1;
        if (a > b)
            return 1;
    }
    return 0;
}

ExtensionStatusKind statusForManifest(const ExtensionManifest &manifest) {
    if (manifest.min_app_version.empty())
        return ExtensionStatusKind::Ok;

    const auto compat = compareVersions(manifest.min_app_version, APP_VERSION);
    if (!compat)
        return ExtensionStatusKind::Incompatible;

    return *compat > 0 ? ExtensionStatusKind::Incompatible : ExtensionStatusKind::Ok;
}

std::optional<std::string> readManifestId(const fs::path &manifest_path) {
    std::ifstream in(manifest_path);
    if (!in)
        return std::nullopt;

    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return std::nullopt;
    if (!j.contains("id") || !j["id"].is_string())
        return std::nullopt;

    const std::string id = j["id"].get<std::string>();
    if (id.empty())
        return std::nullopt;

    return id;
}

} // namespace

std::vector<fs::path> ExtensionManager::scanRoots(const fs::path &project_root) const {
    std::vector<fs::path> roots;
    roots.push_back(fs::path(PROJECT_SOURCE_DIR) / "extensions");
#ifdef _WIN32
    if (const char *home = std::getenv("USERPROFILE"))
        roots.push_back(fs::path(home) / ".rf-sim" / "extensions");
#else
    if (const char *home = std::getenv("HOME"))
        roots.push_back(fs::path(home) / ".rf-sim" / "extensions");
#endif
    roots.push_back(project_root / "rf-sim-extensions");
    return roots;
}

void ExtensionManager::loadRoot(const fs::path &root) {
    try {
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
            return;

        std::vector<fs::path> manifest_paths;
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            return;

        const fs::directory_iterator end;
        while (it != end) {
            const fs::directory_entry entry = *it;

            std::error_code entry_ec;
            if (entry.is_directory(entry_ec) && !entry_ec) {
                const fs::path manifest_path = entry.path() / "plugin.json";
                if (fs::exists(manifest_path, entry_ec) && !entry_ec)
                    manifest_paths.push_back(manifest_path);
            }

            it.increment(ec);
            if (ec) {
                ec.clear();
                if (it == end)
                    break;
            }
        }

        std::sort(manifest_paths.begin(), manifest_paths.end());

        for (const auto &manifest_path : manifest_paths) {
            ExtensionRecord record;
            record.manifest_path = manifest_path;
            record.manifest = parseExtensionManifest(manifest_path, record.issues);
            if (record.manifest)
                record.status = statusForManifest(*record.manifest);

            const std::optional<std::string> extension_id =
                record.manifest ? std::optional<std::string>(record.manifest->id)
                                : readManifestId(manifest_path);

            if (extension_id) {
                const auto existing = m_records_by_id.find(*extension_id);
                if (existing != m_records_by_id.end()) {
                    m_records[existing->second] = std::move(record);
                    continue;
                }
                m_records_by_id.emplace(*extension_id, m_records.size());
            }

            m_records.push_back(std::move(record));
        }
    } catch (const fs::filesystem_error &) {
        return;
    }
}

void ExtensionManager::rescan(const fs::path &project_root) {
    m_records.clear();
    m_records_by_id.clear();
    for (const auto &root : scanRoots(project_root))
        loadRoot(root);
}

std::vector<const ExtensionManifest *> ExtensionManager::dataPacks() const {
    std::vector<const ExtensionManifest *> result;
    for (const auto &record : m_records) {
        if (record.status == ExtensionStatusKind::Ok && record.manifest &&
            record.manifest->kind == ExtensionKind::DataPack) {
            result.push_back(&*record.manifest);
        }
    }
    return result;
}

std::vector<const ExtensionManifest *> ExtensionManager::externalTools() const {
    std::vector<const ExtensionManifest *> result;
    for (const auto &record : m_records) {
        if (record.status == ExtensionStatusKind::Ok && record.manifest &&
            record.manifest->kind == ExtensionKind::ExternalTool) {
            result.push_back(&*record.manifest);
        }
    }
    return result;
}
