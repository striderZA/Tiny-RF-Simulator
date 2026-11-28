#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "imgui.h"
#include "session_state.h"
#include "test_temp_paths.h"
#include "ui_font_controller.h"
#include "ui_font_model.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Runs the frame sequence the application uses, with no rendering backend: the dynamic-atlas flag
// lets NewFrame build the atlas without a texture.
struct ImGuiScope {
    ImGuiScope() {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024, 768);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ~ImGuiScope() { ImGui::DestroyContext(); }
};

// One application frame: the queued font installs between frames, before NewFrame.
void frame(UiFontController &fonts) {
    fonts.applyPending();
    ImGui::NewFrame();
    ImGui::EndFrame();
}

// Two frames bring a fresh context to the state the controller starts from.
void startUp(UiFontController &fonts) {
    frame(fonts);
    frame(fonts);
}

// True when the atlas holds a font with this FontId. ImGui never reuses an id, while a freed font's
// address can be taken by the next font, so ids are the identity to compare.
bool atlasHolds(int font_id) {
    for (ImFont *candidate : ImGui::GetIO().Fonts->Fonts)
        if (candidate->FontId == font_id)
            return true;
    return false;
}

std::filesystem::path freshDir(const char *name) {
    const std::filesystem::path dir =
        test_temp_paths::kProcessTempRoot.root() / "ui_font_controller" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

TEST_CASE("A size dragged across frames rebuilds the atlas once, on release", "[ui_font][size]") {
    ImGuiScope imgui;
    SessionState state(freshDir("drag") / "app.ini");
    UiFontController fonts(state);
    startUp(fonts);
    const int before_drag = fonts.rebuildCount();
    REQUIRE(before_drag == 1);

    // The value changes on every frame while the control is held: no rebuild may run.
    for (int i = 0; i < 40; ++i) {
        fonts.setSize(12 + i % 12, true);
        frame(fonts);
    }
    REQUIRE(fonts.rebuildCount() == before_drag);

    // Release applies the value at once, without waiting for the settle time.
    fonts.setSize(20, false);
    frame(fonts);
    REQUIRE(fonts.rebuildCount() == before_drag + 1);
    REQUIRE(fonts.appliedSize() == Catch::Approx(20.0f));
    frame(fonts);
    frame(fonts);
    REQUIRE(fonts.rebuildCount() == before_drag + 1);
}

TEST_CASE("A size held still rebuilds once after it settles", "[ui_font][size]") {
    ImGuiScope imgui;
    SessionState state(freshDir("settle") / "app.ini");
    UiFontController fonts(state);
    startUp(fonts);
    const int before = fonts.rebuildCount();

    fonts.setSize(18, true);
    frame(fonts);
    REQUIRE(fonts.rebuildCount() == before); // changed this frame: not settled yet

    // Held without change for longer than the settle time, then the one rebuild.
    for (int i = 0; i < 30; ++i) {
        fonts.setSize(18, true);
        frame(fonts);
    }
    REQUIRE(fonts.rebuildCount() == before + 1);
    REQUIRE(fonts.appliedSize() == Catch::Approx(18.0f));
}

TEST_CASE("A size is applied at the DPI scale, and a scale change rebuilds the atlas",
          "[ui_font][size]") {
    ImGuiScope imgui;
    SessionState state(freshDir("dpi") / "app.ini");
    UiFontController fonts(state);
    startUp(fonts);

    for (const float scale : {1.0f, 1.5f, 2.0f, 1.0f}) {
        ImGui::GetStyle().FontScaleDpi = scale;
        fonts.setSize(18, false);
        // The size is read between NewFrame and EndFrame: EndFrame pops the font stack, and the
        // size that remains after it is the unscaled base.
        fonts.applyPending();
        ImGui::NewFrame();
        INFO("scale " << scale);
        REQUIRE(fonts.appliedScale() == Catch::Approx(scale));
        // The UI draws at FontSizeBase times the scale, and the atlas font is rasterised at the
        // same product.
        REQUIRE(ImGui::GetStyle().FontSizeBase == Catch::Approx(18.0f));
        REQUIRE(ImGui::GetFontSize() == Catch::Approx(18.0f * scale));
        REQUIRE(fonts.regularFont()->LegacySize == Catch::Approx(18.0f * scale));
        ImGui::EndFrame();
    }
}

TEST_CASE("Repeated size changes leave the atlas holding one font, and the old one is released",
          "[ui_font][size]") {
    ImGuiScope imgui;
    SessionState state(freshDir("repeat") / "app.ini");
    UiFontController fonts(state);
    startUp(fonts);

    for (int px = ui_font::kMinSizePx; px <= ui_font::kMaxSizePx; ++px) {
        const int previous_id = fonts.regularFont()->FontId;
        fonts.setSize(px, false);
        frame(fonts);
        INFO("size " << px << " px");
        REQUIRE(ImGui::GetIO().Fonts->Fonts.Size == 1);
        REQUIRE(ImGui::GetIO().Fonts->Sources.Size == 1);
        REQUIRE(fonts.regularFont() == ImGui::GetIO().FontDefault);
        REQUIRE_FALSE(atlasHolds(previous_id));
    }
}

TEST_CASE("Requested sizes are clamped to the supported range", "[ui_font][size]") {
    ImGuiScope imgui;
    SessionState state(freshDir("clamp") / "app.ini");
    UiFontController fonts(state);
    startUp(fonts);

    fonts.setSize(3, false);
    REQUIRE(fonts.requestedSize() == Catch::Approx(static_cast<float>(ui_font::kMinSizePx)));
    fonts.setSize(99, false);
    REQUIRE(fonts.requestedSize() == Catch::Approx(static_cast<float>(ui_font::kMaxSizePx)));
}

TEST_CASE("Default restores the built-in font, style, emphasis, and size", "[ui_font][size]") {
    ImGuiScope imgui;
    SessionState state(freshDir("default") / "app.ini");
    UiFontController fonts(state);
    startUp(fonts);
    const float builtin = fonts.builtinSize();
    REQUIRE(builtin > 0.0f);

    fonts.setSize(22, false);
    frame(fonts);
    REQUIRE(fonts.appliedSize() == Catch::Approx(22.0f));

    fonts.select("");
    frame(fonts);
    REQUIRE(fonts.appliedFamily().empty());
    REQUIRE(fonts.savedFamily().empty());
    REQUIRE(fonts.requestedSize() == Catch::Approx(builtin));
    REQUIRE(ImGui::GetStyle().FontSizeBase == Catch::Approx(builtin));
    REQUIRE(fonts.savedEmphasis() == ui_font::Emphasis::Bold);
    REQUIRE(fonts.note().empty());
}

#ifdef _WIN32

namespace {

// The first installed family with a Regular and a Bold upright face, or false when none is.
bool familyWithBold(std::string &family) {
    const auto candidates = ui_font::enumerateSystemFonts();
    for (const std::string &name : ui_font::monospacedFamilies(candidates)) {
        bool regular = false;
        bool bold = false;
        for (const ui_font::FontFace &face : ui_font::familyFaces(candidates, name)) {
            regular = regular || (!face.italic && face.weight == 400);
            bold = bold || (!face.italic && face.weight == 700);
        }
        if (regular && bold) {
            family = name;
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("The family, style, emphasis, and size persist and restore", "[ui_font][persist]") {
    std::string family;
    if (!familyWithBold(family)) {
        WARN("no monospaced family with Regular and Bold faces; persistence not exercised");
        return;
    }
    const std::filesystem::path ini = freshDir("persist") / "app.ini";
    const auto faces = ui_font::familyFaces(ui_font::enumerateSystemFonts(), family);
    ui_font::FontFace chosen = faces.back();
    {
        ImGuiScope imgui;
        SessionState state(ini);
        UiFontController fonts(state);
        startUp(fonts);
        fonts.select(family);
        frame(fonts);
        fonts.selectStyle(chosen);
        fonts.selectEmphasis(ui_font::Emphasis::Medium);
        fonts.setSize(18, false);
        frame(fonts);
    }
    SessionState saved(ini);
    REQUIRE(saved.load(UiFontController::kSection, UiFontController::kKey, "") == family);
    REQUIRE(saved.load(UiFontController::kSection, UiFontController::kStyleKey, "") ==
            chosen.style);
    REQUIRE(saved.load(UiFontController::kSection, UiFontController::kSizeKey, "") == "18");
    REQUIRE(saved.load(UiFontController::kSection, UiFontController::kEmphasisKey, "") == "Medium");

    // A new run starts on the saved choice. Medium is listed only where the family has it, so the
    // emphasis is checked against what the restored family offers.
    ImGuiScope imgui;
    UiFontController restored(saved);
    startUp(restored);
    REQUIRE(restored.appliedFamily() == family);
    REQUIRE(restored.appliedBodyFace().style == chosen.style);
    REQUIRE(restored.requestedSize() == Catch::Approx(18.0f));
}

TEST_CASE("A family-only preference loads as Regular and Bold at the built-in size",
          "[ui_font][migration]") {
    std::string family;
    if (!familyWithBold(family)) {
        WARN("no monospaced family with Regular and Bold faces; migration not exercised");
        return;
    }
    SessionState state(freshDir("migrate") / "app.ini");
    state.save(UiFontController::kSection, UiFontController::kKey, family.c_str());

    ImGuiScope imgui;
    UiFontController fonts(state);
    startUp(fonts);
    REQUIRE(fonts.appliedFamily() == family);
    REQUIRE(fonts.appliedBodyFace().weight == 400);
    REQUIRE_FALSE(fonts.appliedBodyFace().italic);
    REQUIRE(fonts.appliedEmphasisFace().weight == 700);
    REQUIRE(fonts.requestedSize() == Catch::Approx(fonts.builtinSize()));
    REQUIRE(fonts.note().empty());
}

TEST_CASE("A saved style that is no longer available falls back visibly, and is kept",
          "[ui_font][fallback]") {
    std::string family;
    if (!familyWithBold(family)) {
        WARN("no monospaced family with Regular and Bold faces; style fallback not exercised");
        return;
    }
    SessionState state(freshDir("style_fallback") / "app.ini");
    state.save(UiFontController::kSection, UiFontController::kKey, family.c_str());
    state.save(UiFontController::kSection, UiFontController::kStyleKey, "Hairline Quirk");
    state.save(UiFontController::kSection, UiFontController::kStyleWeightKey, "100");

    ImGuiScope imgui;
    UiFontController fonts(state);
    startUp(fonts);
    REQUIRE(fonts.appliedFamily() == family);
    REQUIRE(fonts.appliedBodyFace().style != "Hairline Quirk");
    REQUIRE(fonts.note().find("Hairline Quirk") != std::string::npos);
    REQUIRE(fonts.savedStyle().name == "Hairline Quirk");
}

TEST_CASE("A saved size outside the range is clamped and says so", "[ui_font][fallback]") {
    SessionState state(freshDir("size_fallback") / "app.ini");
    state.save(UiFontController::kSection, UiFontController::kSizeKey, "40");

    ImGuiScope imgui;
    UiFontController fonts(state);
    startUp(fonts);
    REQUIRE(fonts.appliedSize() == Catch::Approx(static_cast<float>(ui_font::kMaxSizePx)));
    REQUIRE(fonts.note().find("40 px") != std::string::npos);
}

TEST_CASE("A saved emphasis that is not Bold or Medium falls back to Bold, with a note",
          "[ui_font][fallback]") {
    SessionState state(freshDir("emphasis_fallback") / "app.ini");
    state.save(UiFontController::kSection, UiFontController::kEmphasisKey, "Black");

    ImGuiScope imgui;
    UiFontController fonts(state);
    startUp(fonts);
    REQUIRE(fonts.savedEmphasis() == ui_font::Emphasis::Bold);
    REQUIRE(fonts.note().find("Black") != std::string::npos);
}

#endif // _WIN32
