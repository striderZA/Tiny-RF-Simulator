#pragma once
#include "imgui.h"

ImTextureID loadTextureFromFile(const char* png_path);
void freeTexture(ImTextureID tex);
