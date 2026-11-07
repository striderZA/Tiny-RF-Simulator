#include "budget_widget.h"

#include "imgui.h"

void BudgetWidget::draw(const char *title, bool *p_open, BudgetConfig &budget,
                        ComponentLibrary &library, const std::vector<double> &frequencies_Hz,
                        const std::vector<double> &simulation_gain_dB,
                        const std::vector<double> &simulation_nf_dB,
                        const std::vector<double> &simulation_output_power_dBm,
                        const std::vector<double> &simulation_iip3_dBm,
                        const ReceiverRequirementsState &requirements,
                        const ReceiverRequirementsEvaluation &simulation_result,
                        double stimulus_power_dBm) {
    (void)library;
    (void)frequencies_Hz;
    (void)simulation_gain_dB;
    (void)simulation_nf_dB;
    (void)simulation_output_power_dBm;
    (void)simulation_iip3_dBm;
    (void)requirements;
    (void)simulation_result;
    (void)stimulus_power_dBm;

    if (!ImGui::Begin(title, p_open))
        ImGui::End();
    else {
        if (budget.stages.empty())
            ImGui::TextDisabled("Add a library part");
        else
            ImGui::TextDisabled("Budget stages: %zu", budget.stages.size());
        ImGui::End();
    }
}
