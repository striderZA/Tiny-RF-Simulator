#include "tutorial_widget.h"

#include "tutorial_state.h"
#include "tutorial_steps.h"
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName

namespace {

constexpr float kGuideWidth = 380.0f;
constexpr float kGuideMargin = 20.0f;
constexpr float kButtonWidth = 82.0f;

// Outlines the step's target panel on the foreground draw list so it sits above
// every regular window. Silently does nothing when the panel is hidden via the
// View menu (FindWindowByName still returns the retained window, but its
// position is stale, hence the WasActive check).
void highlightTarget(TutorialTarget target) {
    const char *title = tutorialTargetWindowTitle(target);
    if (!title)
        return;

    ImGuiWindow *win = ImGui::FindWindowByName(title);
    if (!win || !win->WasActive)
        return;

    ImVec2 min = win->Pos;
    ImVec2 max = ImVec2(win->Pos.x + win->Size.x, win->Pos.y + win->Size.y);
    ImGui::GetForegroundDrawList()->AddRect(min, max, IM_COL32(255, 200, 0, 255), 4.0f, 0, 3.0f);
}

} // namespace

void TutorialWidget::draw(TutorialState &state) {
    if (!state.isActive())
        return;

    const TutorialStep &step = state.currentStep();
    highlightTarget(step.target);

    // Pinned to the bottom-right of the main viewport: the highlight is drawn on
    // the foreground list and would otherwise paint over this window.
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kGuideMargin,
                                   vp->WorkPos.y + vp->WorkSize.y - kGuideMargin),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(kGuideWidth, 0.0f), ImGuiCond_Always);

    if (!ImGui::Begin("Tutorial Guide", nullptr,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Step %d of %d", state.stepIndex() + 1, state.stepCount());
    ImGui::Text("%s", step.title);
    ImGui::Separator();
    ImGui::TextWrapped("%s", step.instruction);
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::BeginDisabled(state.atFirstStep());
    if (ImGui::Button("Back", ImVec2(kButtonWidth, 0)))
        state.back();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(state.atLastStep() ? "Finish" : "Next", ImVec2(kButtonWidth, 0)))
        state.next();

    ImGui::SameLine();
    ImGui::BeginDisabled(state.atLastStep());
    if (ImGui::Button("Skip", ImVec2(kButtonWidth, 0)))
        state.skipToLast();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Exit", ImVec2(kButtonWidth, 0)))
        state.exit();

    ImGui::End();
}
