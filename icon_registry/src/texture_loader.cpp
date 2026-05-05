#include "texture_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <windows.h>
#include <GL/gl.h>

ImTextureID loadTextureFromFile(const char* png_path) {
    int w, h, channels;
    unsigned char* data = stbi_load(png_path, &w, &h, &channels, 4);
    if (!data)
        return ImTextureID(0);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return ImTextureID(static_cast<ImU64>(tex));
}

void freeTexture(ImTextureID tex) {
    if (!tex) return;
    GLuint gl_tex = static_cast<GLuint>(static_cast<ImU64>(tex));
    glDeleteTextures(1, &gl_tex);
}
