#include "icon_registry.h"
#include "texture_loader.h"
#include <algorithm>

struct IconRegistry::Impl {
    std::unordered_map<std::string, ImTextureID> icons;
};

IconRegistry::IconRegistry() : m_impl(new Impl()) {}

IconRegistry::~IconRegistry() {
    clear();
    delete m_impl;
}

void IconRegistry::load(const std::string& label_prefix, const char* png_path) {
    ImTextureID tex = loadTextureFromFile(png_path);
    if (tex)
        m_impl->icons[label_prefix] = tex;
}

ImTextureID IconRegistry::get(const std::string& node_label) const {
    for (const auto& [prefix, tex] : m_impl->icons) {
        if (node_label.rfind(prefix, 0) == 0)
            return tex;
    }
    return ImTextureID(0);
}

void IconRegistry::clear() {
    for (auto& [prefix, tex] : m_impl->icons)
        freeTexture(tex);
    m_impl->icons.clear();
}
