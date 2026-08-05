#pragma once

#include "imgui.h"
#include <filesystem>
#include <string>

class ImageTexture {
  public:
    ~ImageTexture();

    bool load(const std::filesystem::path &path, std::string &error);
    void reset();

    ImTextureID id() const { return m_texture; }
    bool loaded() const { return m_texture != ImTextureID(0); }

  private:
    ImTextureID m_texture = ImTextureID(0);
};
