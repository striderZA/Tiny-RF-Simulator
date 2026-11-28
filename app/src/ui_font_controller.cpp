#include "ui_font_controller.h"

#include "imgui.h"
#include "logging_core.h"
#include "session_state.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

namespace {

std::string quotedName(const std::string &family) { return "\"" + family + "\""; }

bool sameFace(const ui_font::FontFace &a, const ui_font::FontFace &b) {
    return a.style == b.style && a.weight == b.weight && a.italic == b.italic;
}

std::string joined(const std::vector<std::string> &parts) {
    std::string text;
    for (const std::string &part : parts) {
        if (!text.empty())
            text += ' ';
        text += part;
    }
    return text;
}

// The saved style weight. Anything but a whole number from 1 to 1000 is the Regular weight.
int parseWeight(const std::string &text) {
    int value = 0;
    const char *last = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), last, value);
    if (parsed.ec != std::errc() || parsed.ptr != last || value < 1 || value > 1000)
        return 400;
    return value;
}

// Atlas name of a face. ImFontConfig::Name is truncated to its buffer.
std::string fontName(const std::string &family, const std::string &style) {
    return family + " " + style;
}

} // namespace

bool UiFontController::Choice::operator==(const Choice &other) const {
    return family == other.family && style.name == other.style.name &&
           style.weight == other.style.weight && style.italic == other.style.italic &&
           emphasis == other.emphasis && size == other.size;
}

UiFontController::UiFontController(SessionState &state) : m_state(state) {
    Choice saved;
    saved.family = state.load(kSection, kKey, "");
    saved.style.name = state.load(kSection, kStyleKey, "");
    saved.style.weight = parseWeight(state.load(kSection, kStyleWeightKey, "400"));
    saved.style.italic = state.load(kSection, kStyleItalicKey, "0") == "1";
    // A saved emphasis or size that cannot be used resolves to the nearest valid choice, and the
    // note says so until the user changes it.
    const ui_font::EmphasisResolution emphasis =
        ui_font::resolveSavedEmphasis(state.load(kSection, kEmphasisKey, ""));
    saved.emphasis = emphasis.emphasis;
    const ui_font::SizeResolution size =
        ui_font::resolveSavedSize(state.load(kSection, kSizeKey, ""));
    saved.size = size.size_px;
    if (!emphasis.note.empty())
        m_load_notes.push_back(emphasis.note);
    if (!size.note.empty())
        m_load_notes.push_back(size.note);
    m_saved = saved;
    m_persisted = saved;
}

void UiFontController::ensureEnumerated() {
    if (m_enumerated)
        return;
    m_enumerated = true;
    m_candidates = ui_font::enumerateSystemFonts();
    m_installed = ui_font::monospacedFamilies(m_candidates);
    LOG_INFO("Settings: %zu monospaced UI font families found", m_installed.size());
}

const std::vector<std::string> &UiFontController::installed() {
    ensureEnumerated();
    return m_installed;
}

float UiFontController::requestedSize() const {
    return m_saved.size > 0 ? static_cast<float>(m_saved.size) : m_builtin_size;
}

void UiFontController::rememberDiscrete() {
    // Keep the state from before the first pending request, so a refused request can restore it.
    if (m_user_request)
        return;
    m_before_request = Discrete{m_saved.family, m_saved.style, m_saved.emphasis};
}

void UiFontController::restoreDiscrete() {
    m_saved.family = m_before_request.family;
    m_saved.style = m_before_request.style;
    m_saved.emphasis = m_before_request.emphasis;
}

void UiFontController::select(const std::string &family) {
    rememberDiscrete();
    m_saved.family = family;
    m_saved.style = ui_font::StyleChoice{};
    if (family.empty()) {
        m_saved.emphasis = ui_font::Emphasis::Bold;
        m_saved.size = 0;
    }
    m_load_notes.clear();
    m_user_request = true;
    m_dirty = true;
}

void UiFontController::selectStyle(const ui_font::FontFace &face) {
    rememberDiscrete();
    m_saved.style.name = face.style;
    m_saved.style.weight = face.weight;
    m_saved.style.italic = face.italic;
    m_load_notes.clear();
    m_user_request = true;
    m_dirty = true;
}

void UiFontController::selectEmphasis(ui_font::Emphasis emphasis) {
    rememberDiscrete();
    m_saved.emphasis = emphasis;
    m_load_notes.clear();
    m_user_request = true;
    m_dirty = true;
}

void UiFontController::setSize(int px, bool active) {
    px = std::clamp(px, ui_font::kMinSizePx, ui_font::kMaxSizePx);
    if (static_cast<float>(px) != requestedSize()) {
        m_saved.size = px;
        m_size_changed_at = ImGui::GetTime();
        m_load_notes.clear();
    }
    m_size_active = active;
}

bool UiFontController::applyPending() {
    // The style carries no font size until the first frame's NewFrame derives it from the default
    // font. Installing before that would add the picked font at size 0, and ImGui would then fall
    // back to its own 20 px default for the whole UI. Keep the request queued for one frame.
    ImGuiStyle &style = ImGui::GetStyle();
    // The test engine restores ImGuiIO after each case, FontDefault included, and that pointer can
    // name a font the atlas already freed. The font this controller owns is always live, so it is
    // re-asserted before any frame draws, ahead of the early returns below.
    if (m_regular != nullptr)
        ImGui::GetIO().FontDefault = m_regular;
    if (style.FontSizeBase <= 0.0f)
        return false;
    if (m_builtin_size <= 0.0f)
        m_builtin_size = style.FontSizeBase;
    ensureEnumerated();

    const bool size_changed =
        requestedSize() != m_applied_size || style.FontScaleDpi != m_applied_scale;
    if (!m_dirty && !size_changed)
        return false;
    // A size held by the slider waits until it has stopped changing for kSizeSettleSeconds, then
    // rebuilds once. A release (m_size_active false) rebuilds at once.
    if (!m_dirty && m_size_active && ImGui::GetTime() - m_size_changed_at < kSizeSettleSeconds)
        return false;

    const bool user = m_user_request;
    m_dirty = false;
    m_user_request = false;
    const bool rebuilt = rebuild(user);
    persist();
    return rebuilt;
}

bool UiFontController::rebuild(bool user) {
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();
    const float size_px = requestedSize();
    const float scale = style.FontScaleDpi;
    // The raster size. The UI draws at FontSizeBase times the DPI scale, so the base is the size
    // at 100% and the atlas font is rasterised at size times the scale.
    const float raster_px = size_px * scale;

    std::vector<std::string> notes = m_load_notes;
    std::string family = m_saved.family;
    ui_font::FacePlan plan;
    std::vector<unsigned char> body_bytes;
    std::vector<unsigned char> emphasis_bytes;

    if (!family.empty() &&
        !ui_font::resolveSavedFamily(family, m_installed).missing_family.empty()) {
        LOG_WARN("Settings: saved UI font %s is not installed; using Default",
                 quotedName(family).c_str());
        notes.push_back("Saved font " + quotedName(family) +
                        " is not installed. Showing Default; the choice is kept and returns when "
                        "the font is installed.");
        family.clear();
    }
    if (!family.empty()) {
        plan = ui_font::planFaces(family, ui_font::familyFaces(m_candidates, family), m_saved.style,
                                  m_saved.emphasis);
        const bool loaded = plan.has_face &&
                            ui_font::readSystemFontFace(family, plan.body, body_bytes) &&
                            (sameFace(plan.body, plan.emphasis) ||
                             ui_font::readSystemFontFace(family, plan.emphasis, emphasis_bytes));
        if (!loaded) {
            LOG_WARN("Settings: UI font %s could not be loaded", quotedName(family).c_str());
            if (user) {
                // A refused pick keeps the applied font and restores the saved choice.
                restoreDiscrete();
                m_note = quotedName(family) + " could not be loaded. " +
                         (m_applied_family.empty() ? std::string("Showing Default.")
                                                   : quotedName(m_applied_family) + " is kept.");
                return false;
            }
            notes.push_back("Saved font " + quotedName(family) +
                            " could not be loaded. Showing Default; the choice is kept.");
            family.clear();
        } else {
            notes.insert(notes.end(), plan.notes.begin(), plan.notes.end());
        }
    }

    const ui_font::FontFace body = family.empty() ? ui_font::FontFace{} : plan.body;
    const ui_font::FontFace emphasis = family.empty() ? ui_font::FontFace{} : plan.emphasis;
    const bool unchanged = family == m_applied_family && size_px == m_applied_size &&
                           scale == m_applied_scale && m_regular != nullptr &&
                           (family.empty() || (sameFace(body, m_applied_body) &&
                                               sameFace(emphasis, m_applied_emphasis)));
    if (unchanged) {
        m_note = joined(notes);
        return false;
    }

    // Between frames only. ClearFonts() frees the previous fonts and their atlas glyphs; the
    // backend releases the old texture after its last use.
    // ClearFonts() makes ImGui re-apply the context's stale base size to the style, so the size is
    // set after it, and again before the fonts are chosen, since AddFontDefault() picks by it.
    io.Fonts->ClearFonts();
    style.FontSizeBase = size_px;

    // The atlas owns a copy, allocated with ImGui's allocator, so ClearFonts() frees it with the
    // font. A non-owned buffer must outlive the font until RemoveFont() or shutdown.
    const auto addFace = [&io](const std::vector<unsigned char> &bytes, const std::string &name,
                               float px) -> ImFont * {
        void *data = ImGui::MemAlloc(bytes.size());
        std::memcpy(data, bytes.data(), bytes.size());
        ImFontConfig config; // FontDataOwnedByAtlas stays true
        config.SizePixels = px;
        std::snprintf(config.Name, sizeof(config.Name), "%s", name.c_str());
        return io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()), px, &config);
    };
    // The built-in font at the built-in size is added exactly as before this feature; any other
    // size uses ImGui's scalable built-in face, since the bitmap built-in font has one size.
    const auto addDefault = [&io, size_px, this]() -> ImFont * {
        if (size_px == m_builtin_size)
            return io.Fonts->AddFontDefault();
        ImFontConfig config;
        config.SizePixels = size_px * ImGui::GetStyle().FontScaleDpi;
        return io.Fonts->AddFontDefaultVector(&config);
    };

    ImFont *regular = nullptr;
    ImFont *emphasis_font = nullptr;
    if (!family.empty()) {
        regular = addFace(body_bytes, fontName(family, body.style), raster_px);
        if (regular == nullptr) {
            LOG_WARN("Settings: UI font %s could not be added to the atlas",
                     quotedName(family).c_str());
            notes.push_back("Saved font " + quotedName(family) +
                            " could not be loaded. Showing Default; the choice is kept.");
            family.clear();
            io.Fonts->ClearFonts();
            style.FontSizeBase = size_px;
        } else if (sameFace(body, emphasis)) {
            emphasis_font = regular;
        } else if (emphasis_bytes == body_bytes) {
            // Named weights of one variable font can share a file, and ImGui reads only its default
            // instance. Such a face would draw as the body face, so bold text uses the body face
            // and the note says so.
            emphasis_font = regular;
            notes.push_back(quotedName(family) + " has no separate bold face. Bold text uses " +
                            quotedName(body.style) + ".");
        } else {
            emphasis_font = addFace(emphasis_bytes, fontName(family, emphasis.style), raster_px);
            if (emphasis_font == nullptr) {
                // The emphasis face is optional: bold text falls back to the body face.
                LOG_WARN("Settings: emphasis face of %s could not be added",
                         quotedName(family).c_str());
                notes.push_back(quotedName(family) +
                                " bold face could not be added. Bold text uses " +
                                quotedName(body.style) + ".");
                emphasis_font = regular;
            }
        }
    }
    if (family.empty()) {
        regular = addDefault();
        emphasis_font = regular;
    }
    style.FontSizeBase = size_px;
    io.FontDefault = regular;
    m_regular = regular;
    m_emphasis_font = emphasis_font;

    m_applied_family = family;
    m_applied_faces = family.empty() ? std::vector<ui_font::FontFace>{}
                                     : ui_font::familyFaces(m_candidates, family);
    m_applied_body = body;
    m_applied_emphasis = emphasis;
    m_applied_size = size_px;
    m_applied_scale = scale;
    m_note = joined(notes);
    ++m_rebuilds;
    LOG_INFO("Settings: UI font %s, size %.1f px at scale %.2f",
             family.empty() ? "Default" : family.c_str(), size_px, scale);
    return true;
}

void UiFontController::persist() {
    if (m_saved == m_persisted)
        return;
    m_state.save(kSection, kKey, m_saved.family.c_str());
    m_state.save(kSection, kStyleKey, m_saved.style.name.c_str());
    m_state.save(kSection, kStyleWeightKey, std::to_string(m_saved.style.weight).c_str());
    m_state.save(kSection, kStyleItalicKey, m_saved.style.italic ? "1" : "0");
    m_state.save(kSection, kEmphasisKey, ui_font::emphasisName(m_saved.emphasis).c_str());
    const std::string size = m_saved.size > 0 ? std::to_string(m_saved.size) : std::string();
    m_state.save(kSection, kSizeKey, size.c_str());
    m_persisted = m_saved;
}

std::vector<const char *> UiFontController::persistedKeys() {
    return {kKey, kStyleKey, kStyleWeightKey, kStyleItalicKey, kEmphasisKey, kSizeKey};
}
