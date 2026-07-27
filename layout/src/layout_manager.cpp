#include "layout_manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <imgui.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

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

} // namespace

LayoutManager::LayoutManager() : m_exe_dir(detectExeDir()) {}

std::string LayoutManager::defaultLayoutPath() const {
    return (fs::path(m_exe_dir) / "rf_simulator_layout.ini").string();
}

std::string LayoutManager::layoutsDir() const {
    return (fs::path(m_exe_dir) / "layouts").string();
}

bool LayoutManager::defaultLayoutExists() const {
    std::error_code ec;
    return fs::exists(defaultLayoutPath(), ec);
}

void LayoutManager::saveDefaultLayout() const {
    ImGui::SaveIniSettingsToDisk(defaultLayoutPath().c_str());
}

bool LayoutManager::loadDefaultLayout() const {
    if (!defaultLayoutExists())
        return false;
    ImGui::LoadIniSettingsFromDisk(defaultLayoutPath().c_str());
    return true;
}

std::string LayoutManager::sanitizeName(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
            out.push_back(c);
    }
    size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos)
        return "Layout";
    size_t end = out.find_last_not_of(' ');
    return out.substr(start, end - start + 1);
}

std::string LayoutManager::namedLayoutPath(const std::string &name) const {
    return (fs::path(layoutsDir()) / (sanitizeName(name) + ".ini")).string();
}

bool LayoutManager::namedLayoutExists(const std::string &name) const {
    std::error_code ec;
    return fs::exists(namedLayoutPath(name), ec);
}

bool LayoutManager::saveNamedLayout(const std::string &name) const {
    std::error_code ec;
    fs::create_directories(layoutsDir(), ec);
    if (ec)
        return false;
    ImGui::SaveIniSettingsToDisk(namedLayoutPath(name).c_str());
    return true;
}

bool LayoutManager::loadNamedLayout(const std::string &name) const {
    if (!namedLayoutExists(name))
        return false;
    ImGui::LoadIniSettingsFromDisk(namedLayoutPath(name).c_str());
    return true;
}

bool LayoutManager::deleteNamedLayout(const std::string &name) const {
    std::error_code ec;
    return fs::remove(namedLayoutPath(name), ec);
}

bool LayoutManager::renameNamedLayout(const std::string &old_name, const std::string &new_name) const {
    if (!namedLayoutExists(old_name))
        return false;
    if (namedLayoutExists(new_name))
        return false;
    std::error_code ec;
    fs::rename(namedLayoutPath(old_name), namedLayoutPath(new_name), ec);
    return !ec;
}

std::vector<std::string> LayoutManager::listNamedLayouts() const {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::exists(layoutsDir(), ec) || !fs::is_directory(layoutsDir(), ec))
        return names;
    for (const auto &entry : fs::directory_iterator(layoutsDir(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ini")
            names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}
