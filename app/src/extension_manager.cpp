#include "extension_manager.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace {

int parseVersionPart(std::string_view part) {
    int value = 0;
    const auto *begin = part.data();
    const auto *end = part.data() + part.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end)
        return 0;
    return value;
}

std::vector<int> parseVersion(std::string_view version) {
    std::vector<int> parts;
    std::size_t start = 0;
    while (start <= version.size()) {
        const std::size_t dot = version.find('.', start);
        const std::size_t count = dot == std::string_view::npos ? version.size() - start : dot - start;
        parts.push_back(parseVersionPart(version.substr(start, count)));
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    return parts;
}

int compareVersions(std::string_view lhs, std::string_view rhs) {
    const auto lhs_parts = parseVersion(lhs);
    const auto rhs_parts = parseVersion(rhs);
    const std::size_t count = std::max(lhs_parts.size(), rhs_parts.size());
    for (std::size_t i = 0; i < count; ++i) {
        const int a = i < lhs_parts.size() ? lhs_parts[i] : 0;
        const int b = i < rhs_parts.size() ? rhs_parts[i] : 0;
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

    return compareVersions(manifest.min_app_version, APP_VERSION) > 0 ?
        ExtensionStatusKind::Incompatible : ExtensionStatusKind::Ok;
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
    if (!fs::exists(root) || !fs::is_directory(root))
        return;

    std::vector<fs::path> manifest_paths;
    for (const auto &entry : fs::directory_iterator(root)) {
        if (!entry.is_directory())
            continue;

        const fs::path manifest_path = entry.path() / "plugin.json";
        if (fs::exists(manifest_path))
            manifest_paths.push_back(manifest_path);
    }

    std::sort(manifest_paths.begin(), manifest_paths.end());

    for (const auto &manifest_path : manifest_paths) {
        ExtensionRecord record;
        record.manifest_path = manifest_path;
        record.manifest = parseExtensionManifest(manifest_path, record.issues);
        if (record.manifest)
            record.status = statusForManifest(*record.manifest);
        m_records.push_back(std::move(record));
    }
}

void ExtensionManager::rescan(const fs::path &project_root) {
    m_records.clear();
    for (const auto &root : scanRoots(project_root))
        loadRoot(root);
}

std::vector<const ExtensionManifest *> ExtensionManager::dataPacks() const {
    std::vector<const ExtensionManifest *> result;
    for (const auto &record : m_records) {
        if (record.manifest && record.manifest->kind == ExtensionKind::DataPack)
            result.push_back(&*record.manifest);
    }
    return result;
}

std::vector<const ExtensionManifest *> ExtensionManager::externalTools() const {
    std::vector<const ExtensionManifest *> result;
    for (const auto &record : m_records) {
        if (record.manifest && record.manifest->kind == ExtensionKind::ExternalTool)
            result.push_back(&*record.manifest);
    }
    return result;
}
