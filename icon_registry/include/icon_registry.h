#pragma once
#include "imgui.h"
#include <string>

class IconRegistry {
  public:
    IconRegistry();
    ~IconRegistry();

    void load(const std::string& label_prefix, const char* png_path);
    ImTextureID get(const std::string& node_label) const;
    void clear();

  private:
    struct Impl;
    Impl* m_impl = nullptr;
};
