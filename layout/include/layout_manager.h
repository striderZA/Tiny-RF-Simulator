#pragma once

#include <string>
#include <vector>

// Manages ImGui window-layout persistence: a single "default" layout that
// ImGui auto-saves/loads via IniFilename, plus named presets the user
// explicitly saves/loads/renames/deletes. All paths are relative to the
// running executable's directory, not the current working directory.
class LayoutManager {
  public:
    LayoutManager();

    // <exe_dir>/rf_simulator_layout.ini
    std::string defaultLayoutPath() const;
    // <exe_dir>/layouts/
    std::string layoutsDir() const;

    bool defaultLayoutExists() const;
    // Writes the current live ImGui layout to defaultLayoutPath().
    // Requires an active ImGuiContext.
    void saveDefaultLayout() const;
    // Returns false (no-op) if the default layout file doesn't exist.
    // Requires an active ImGuiContext.
    bool loadDefaultLayout() const;

    // Creates layoutsDir() if missing. Requires an active ImGuiContext.
    bool saveNamedLayout(const std::string &name) const;
    // Returns false (no-op) if the named preset doesn't exist.
    // Requires an active ImGuiContext.
    bool loadNamedLayout(const std::string &name) const;
    bool deleteNamedLayout(const std::string &name) const;
    // Returns false if old_name is missing or new_name already exists.
    bool renameNamedLayout(const std::string &old_name, const std::string &new_name) const;
    bool namedLayoutExists(const std::string &name) const;
    // Sorted preset names, extension stripped. Empty if layoutsDir() doesn't exist.
    std::vector<std::string> listNamedLayouts() const;

    // Keeps only [A-Za-z0-9-_ ], trims leading/trailing spaces.
    // Returns "Layout" if the result would be empty.
    static std::string sanitizeName(const std::string &name);

  private:
    std::string m_exe_dir;
    std::string namedLayoutPath(const std::string &name) const;
};
