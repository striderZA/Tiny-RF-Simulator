#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "session_state.h"
#include "test_temp_paths.h"
#include "ui_font_controller.h"
#include "ui_font_model.h"

#include <filesystem>
#include <vector>

#ifdef _WIN32

namespace {

// Builds the same frame sequence the application uses and returns the style's base font size.
// The first frame runs no install (applyPending() before NewFrame), exactly as at startup.
void beginFrames(ImGuiIO &io) {
    io.DisplaySize = ImVec2(1024, 768);
    io.DeltaTime = 1.0f / 60.0f;
    // No rendering backend is attached: the dynamic-atlas flag lets NewFrame build the atlas
    // without a texture, which is all this test needs.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

float builtinStartupSize() {
    ImGui::CreateContext();
    beginFrames(ImGui::GetIO());
    float base = 0.0f;
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        base = ImGui::GetStyle().FontSizeBase;
        ImGui::EndFrame();
    }
    ImGui::DestroyContext();
    return base;
}

} // namespace

TEST_CASE("A saved UI font keeps the built-in size at startup", "[ui_font][startup]") {
    // Platform facts: the enumeration needs an installed monospaced family to restore.
    const std::vector<std::string> families =
        ui_font::monospacedFamilies(ui_font::enumerateSystemFonts());
    if (families.empty()) {
        WARN("no monospaced families installed; startup restore not exercised");
        return;
    }
    const float builtin = builtinStartupSize();
    REQUIRE(builtin > 0.0f);

    const std::filesystem::path dir = test_temp_paths::kProcessTempRoot.root() / "ui_font_startup";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    SessionState state(dir / "app.ini");
    state.save("Settings", "UiFont", families.front().c_str());

    UiFontController fonts(state);
    ImGui::CreateContext();
    beginFrames(ImGui::GetIO());
    for (int frame = 0; frame < 3; ++frame) {
        fonts.applyPending();
        ImGui::NewFrame();
        ImGui::EndFrame();
    }

    INFO("installed family: " << families.front());
    INFO("built-in startup size: " << builtin);
    REQUIRE(fonts.appliedFamily() == families.front());
    REQUIRE(ImGui::GetStyle().FontSizeBase == Catch::Approx(builtin));
    REQUIRE(ImGui::GetFontSize() == Catch::Approx(builtin));
    // The body font, plus the emphasis font when the family has a separate Bold face.
    const std::size_t slots = fonts.regularFont() == fonts.emphasisFont() ? 1 : 2;
    REQUIRE(ImGui::GetIO().Fonts->Fonts.Size == static_cast<int>(slots));

    ImGui::DestroyContext();
    std::filesystem::remove_all(dir, ec);
}

#endif // _WIN32
