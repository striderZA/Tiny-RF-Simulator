#pragma once

#include <string>
#include <functional>

class ComponentLibrary;
struct ComponentDefinition;

class LibraryBrowserWidget {
public:
    explicit LibraryBrowserWidget(ComponentLibrary& library);
    void draw(const char* title, bool* p_open = nullptr);
    std::function<void(const ComponentDefinition&)> onInsert;
private:
    ComponentLibrary* m_library;
    char m_filter_buffer[256] = {};
    bool matchesFilter(const ComponentDefinition& def) const;
};
