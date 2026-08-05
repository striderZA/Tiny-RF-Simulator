#include "image_texture.h"
#include "../../icon_registry/src/texture_loader.h"

ImageTexture::~ImageTexture() { reset(); }

bool ImageTexture::load(const std::filesystem::path &path, std::string &error) {
    reset();
    m_texture = loadTextureFromFile(path.string().c_str());
    if (!m_texture) {
        error = "failed to load texture";
        return false;
    }
    error.clear();
    return true;
}

void ImageTexture::reset() {
    if (!m_texture)
        return;
    freeTexture(m_texture);
    m_texture = ImTextureID(0);
}
