#include "amplifier_digitizer_widget.h"
#include "imgui.h"

bool AmplifierDigitizerWidget::draw(bool *open) {
    if (open && !*open)
        return false;

    bool keep_open = true;
    bool *window_open = open ? open : &keep_open;
    if (!ImGui::Begin("Amplifier Datasheet Import", window_open)) {
        ImGui::End();
        return false;
    }

    ImGui::TextUnformatted("Amplifier datasheet import wizard");
    ImGui::Text("Step %d", m_step + 1);
    ImGui::Separator();
    ImGui::TextUnformatted("Shell UI only in this task; calibration/editing comes from the model "
                           "and later wiring tasks.");

    if (ImGui::Button("Previous") && m_step > 0)
        --m_step;
    ImGui::SameLine();
    if (ImGui::Button("Next") && m_step < 5)
        ++m_step;
    ImGui::SameLine();
    if (ImGui::Button("Export"))
        m_export_requested = true;

    ImGui::End();
    return true;
}
