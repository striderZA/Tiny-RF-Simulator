#include "ui_font_model.h"

namespace ui_font {

// Platforms without a font enumeration (or a build without fontconfig) offer only Default.
std::vector<FontCandidate> enumerateSystemFonts() { return {}; }

bool readSystemFontFile(const std::string &, std::vector<unsigned char> &) { return false; }

} // namespace ui_font
