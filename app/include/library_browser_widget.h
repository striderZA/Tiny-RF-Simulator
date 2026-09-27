#pragma once

#include <functional>
#include <string>

class ComponentLibrary;
struct ComponentDefinition;

class LibraryBrowserWidget {
  public:
    explicit LibraryBrowserWidget(ComponentLibrary &library);
    void draw(const char *title, bool *p_open = nullptr);
    std::function<void(const ComponentDefinition &)> onInsert;
    std::function<void()> onNewComponent;
    std::function<void(const ComponentDefinition &)> onEditComponent;
    // Package export/import. The widget only requests the action; the app owns
    // the native dialogs and the library_package calls, then reports back with
    // setStatus(). The widget deliberately never includes library_package.h
    // internals (tests link the package library without miniz).
    std::function<void()> onExportPackage;
    std::function<void()> onImportPackage;
    void setStatus(std::string status);
    const std::string &status() const { return m_status; }

  private:
    ComponentLibrary *m_library;
    char m_filter_buffer[256] = {};
    std::string m_status;
    bool matchesFilter(const ComponentDefinition &def) const;
};
