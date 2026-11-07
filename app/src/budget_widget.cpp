#include "budget_widget.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kBudgetColumnCount = 10;
constexpr const char *kUnavailable = "\xE2\x80\x94"; // em dash

bool isSupportedBudgetType(const std::string &type) {
    // The registry names the ideal filter `filter`; `ideal_filter` is accepted as an alias
    // and normalized on insert.
    return type == "amplifier" || type == "attenuator" || type == "filter" ||
           type == "ideal_filter" || type == "equalizer" || type == "coax";
}

BudgetStage stageFromDefinition(const ComponentDefinition &definition) {
    BudgetStage stage;
    stage.type = definition.type;
    stage.part_number = definition.part_number;
    stage.manufacturer = definition.manufacturer;
    stage.description = definition.description;
    stage.parameters = definition.parameters;
    stage.data_files = definition.data_files;
    stage.source_path = definition.source_path;
    return stage;
}

// Index of the grid point nearest to `hz`, clamped to the grid ends.
std::size_t nearestIndex(const std::vector<double> &frequencies_Hz, double hz) {
    if (frequencies_Hz.size() <= 1 || !(hz > frequencies_Hz.front()))
        return 0;
    if (hz >= frequencies_Hz.back())
        return frequencies_Hz.size() - 1;

    const auto upper = std::lower_bound(frequencies_Hz.begin(), frequencies_Hz.end(), hz);
    const std::size_t high = static_cast<std::size_t>(upper - frequencies_Hz.begin());
    const std::size_t low = high - 1;
    return (hz - frequencies_Hz[low]) <= (frequencies_Hz[high] - hz) ? low : high;
}

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Same inputs as the evaluator's cache key: any stage snapshot, grid, or stimulus change
// invalidates both the per-stage evaluation and the cascade result.
std::string resultKey(const BudgetConfig &config, const std::vector<double> &frequencies_Hz,
                      double stimulus_power_dBm) {
    std::ostringstream key;
    key << std::setprecision(17);
    key << budgetToJson(config).dump() << '|' << stimulus_power_dBm << '|';
    for (const double frequency : frequencies_Hz)
        key << frequency << ',';
    return key.str();
}

void drawValue(bool available, double value) {
    if (!available || !std::isfinite(value)) {
        ImGui::TextUnformatted(kUnavailable);
        return;
    }
    ImGui::Text("%.3f", value);
}

} // namespace

bool BudgetWidget::addStage(BudgetConfig &budget, const ComponentDefinition &definition) {
    if (!isSupportedBudgetType(definition.type))
        return false;
    BudgetStage stage = stageFromDefinition(definition);
    // The library and evaluator use the registry name for the ideal filter.
    if (stage.type == "ideal_filter")
        stage.type = "filter";
    budget.stages.push_back(std::move(stage));
    if (onChange)
        onChange();
    return true;
}

void BudgetWidget::removeStage(BudgetConfig &budget, std::size_t index) {
    if (index >= budget.stages.size())
        return;
    budget.stages.erase(budget.stages.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_selected_stage == static_cast<int>(index))
        m_selected_stage = -1;
    else if (m_selected_stage > static_cast<int>(index))
        --m_selected_stage;
    if (onChange)
        onChange();
}

bool BudgetWidget::moveStage(BudgetConfig &budget, std::size_t index, int delta) {
    if (index >= budget.stages.size() || delta == 0)
        return false;
    const auto last = static_cast<int>(budget.stages.size()) - 1;
    const int target = static_cast<int>(index) + delta;
    if (target < 0 || target > last)
        return false;

    std::swap(budget.stages[index], budget.stages[static_cast<std::size_t>(target)]);
    // The selection follows the moved stage, so repeated Up/Down keep acting on it.
    if (m_selected_stage == static_cast<int>(index))
        m_selected_stage = target;
    else if (m_selected_stage == target)
        m_selected_stage = static_cast<int>(index);
    if (onChange)
        onChange();
    return true;
}

void BudgetWidget::clearStages(BudgetConfig &budget) {
    budget.stages.clear();
    m_selected_stage = -1;
    if (onChange)
        onChange();
}

std::size_t BudgetWidget::stageCount(const BudgetConfig &budget) const {
    return budget.stages.size();
}

void BudgetWidget::setCursor_Hz(double hz) {
    m_cursor_Hz = hz;
    m_cursor_set = true;
    if (!m_frequencies_Hz.empty()) {
        m_cursor_index = nearestIndex(m_frequencies_Hz, hz);
        m_cursor_Hz = m_frequencies_Hz[m_cursor_index];
    }
}

double BudgetWidget::cursor_Hz() const { return m_cursor_Hz; }

void BudgetWidget::draw(const char *title, bool *p_open, BudgetConfig &budget,
                        ComponentLibrary &library, const std::vector<double> &frequencies_Hz,
                        const std::vector<double> &simulation_gain_dB,
                        const std::vector<double> &simulation_nf_dB,
                        const std::vector<double> &simulation_output_power_dBm,
                        const std::vector<double> &simulation_iip3_dBm,
                        const ReceiverRequirementsState &requirements,
                        const ReceiverRequirementsEvaluation &simulation_result,
                        double stimulus_power_dBm) {
    (void)simulation_gain_dB;
    (void)simulation_nf_dB;
    (void)simulation_output_power_dBm;
    (void)simulation_iip3_dBm;
    (void)simulation_result;

    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    if (m_frequencies_Hz != frequencies_Hz)
        m_frequencies_Hz = frequencies_Hz;

    if (!frequencies_Hz.empty()) {
        if (!m_cursor_set || !std::isfinite(m_cursor_Hz)) {
            double default_Hz = 0.0;
            const auto &config = requirements.config;
            if (config && std::isfinite(config->band_start_Hz) &&
                std::isfinite(config->band_stop_Hz) &&
                config->band_stop_Hz > config->band_start_Hz) {
                default_Hz = 0.5 * (config->band_start_Hz + config->band_stop_Hz);
            } else if (frequencies_Hz.size() == 1) {
                default_Hz = frequencies_Hz.front();
            } else {
                default_Hz = 0.5 * (frequencies_Hz.front() + frequencies_Hz.back());
            }
            m_cursor_Hz = default_Hz;
            m_cursor_set = true;
        }
        m_cursor_index = nearestIndex(frequencies_Hz, m_cursor_Hz);
        m_cursor_Hz = frequencies_Hz[m_cursor_index];
    }

    if (m_selected_stage >= static_cast<int>(budget.stages.size()))
        m_selected_stage = -1;

    // --- Toolbar -----------------------------------------------------------------
    const bool has_selection =
        m_selected_stage >= 0 && m_selected_stage < static_cast<int>(budget.stages.size());
    const auto selected = static_cast<std::size_t>(m_selected_stage);

    if (ImGui::Button("Add part"))
        ImGui::OpenPopup("##budget_add_part");
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_selection);
    if (ImGui::Button("Remove"))
        removeStage(budget, selected);
    ImGui::SameLine();
    if (ImGui::Button("Up"))
        moveStage(budget, selected, -1);
    ImGui::SameLine();
    if (ImGui::Button("Down"))
        moveStage(budget, selected, 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(budget.stages.empty());
    if (ImGui::Button("Clear"))
        clearStages(budget);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    double cursor_Hz = m_cursor_Hz;
    if (ImGui::InputDouble("Cursor (Hz)", &cursor_Hz, 0.0, 0.0, "%.6g"))
        setCursor_Hz(cursor_Hz);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputDouble("Input power (dBm)", &budget.input_power_dBm, 0.0, 0.0, "%.2f") &&
        onChange)
        onChange();

    if (ImGui::BeginPopup("##budget_add_part")) {
        ImGui::TextUnformatted("Add library part");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputTextWithHint("##budget_part_filter", "Filter by part, maker, or type",
                                 m_part_filter.data(), m_part_filter.size());
        const std::string needle = lowerCopy(m_part_filter.data());

        if (ImGui::BeginChild("##budget_part_list", ImVec2(460.0f, 320.0f), true)) {
            const auto definitions = library.all();
            bool any = false;
            for (std::size_t i = 0; i < definitions.size(); ++i) {
                const ComponentDefinition *definition = definitions[i];
                if (!definition || !isSupportedBudgetType(definition->type))
                    continue;

                std::string haystack = definition->part_number + " " + definition->manufacturer +
                                       " " + definition->description + " " + definition->type;
                if (!needle.empty() && lowerCopy(haystack).find(needle) == std::string::npos)
                    continue;

                std::string label = definition->part_number;
                if (!definition->manufacturer.empty())
                    label += " \xE2\x80\x94 " + definition->manufacturer;
                label += " (" + definition->type + ")";

                any = true;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(label.c_str())) {
                    if (addStage(budget, *definition))
                        m_selected_stage = static_cast<int>(budget.stages.size()) - 1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            if (!any)
                ImGui::TextDisabled("No matching library parts");
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    // --- Evaluation --------------------------------------------------------------
    const std::string key = resultKey(budget, frequencies_Hz, stimulus_power_dBm);
    if (key != m_result_key) {
        m_evaluations = m_evaluator.evaluate(budget, library, frequencies_Hz, stimulus_power_dBm);
        std::vector<BudgetStageMetrics> stage_metrics;
        stage_metrics.reserve(m_evaluations.size());
        for (const BudgetStageEvaluation &evaluation : m_evaluations)
            stage_metrics.push_back(evaluation.metrics);
        m_budget_result = computeBudget(budget, frequencies_Hz, stage_metrics);
        m_result_key = key;
    }

    const bool have_point = !frequencies_Hz.empty() && m_cursor_index < frequencies_Hz.size();

    // --- Stage table -------------------------------------------------------------
    const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    if (ImGui::BeginTable("BudgetStages", kBudgetColumnCount, table_flags)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Library part");
        ImGui::TableSetupColumn("Gain (dB)");
        ImGui::TableSetupColumn("NF (dB)");
        ImGui::TableSetupColumn("OIP3 (dBm)");
        ImGui::TableSetupColumn("P1dB (dBm)");
        ImGui::TableSetupColumn("Cum. gain");
        ImGui::TableSetupColumn("Cum. NF");
        ImGui::TableSetupColumn("Cum. OIP3");
        ImGui::TableSetupColumn("Cum. P1dB");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < budget.stages.size(); ++i) {
            const BudgetStage &stage = budget.stages[i];
            const BudgetStageEvaluation *evaluation =
                i < m_evaluations.size() ? &m_evaluations[i] : nullptr;
            const bool unresolved = evaluation && !evaluation->resolved;

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string row_label = std::to_string(i + 1);
            if (ImGui::Selectable(row_label.c_str(), m_selected_stage == static_cast<int>(i),
                                  ImGuiSelectableFlags_SpanAllColumns))
                m_selected_stage = static_cast<int>(i);

            ImGui::TableSetColumnIndex(1);
            if (unresolved) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                std::string label = stage.part_number;
                if (!evaluation->diagnostic.empty())
                    label += " \xE2\x80\x94 " + evaluation->diagnostic;
                ImGui::TextUnformatted(label.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextUnformatted(stage.part_number.c_str());
            }

            bool stage_point = have_point && evaluation != nullptr;
            bool in_band = false;
            if (stage_point) {
                const BudgetStageMetrics &metrics = evaluation->metrics;
                stage_point = m_cursor_index < metrics.gain_dB.size() &&
                              m_cursor_index < metrics.nf_dB.size() &&
                              m_cursor_index < metrics.oip3_dBm.size() &&
                              m_cursor_index < metrics.p1db_dBm.size() &&
                              m_cursor_index < metrics.out_of_band.size();
                in_band = stage_point && !metrics.out_of_band[m_cursor_index];
            }

            ImGui::TableSetColumnIndex(2);
            drawValue(in_band, stage_point ? evaluation->metrics.gain_dB[m_cursor_index]
                                           : std::numeric_limits<double>::quiet_NaN());
            ImGui::TableSetColumnIndex(3);
            drawValue(in_band, stage_point ? evaluation->metrics.nf_dB[m_cursor_index]
                                           : std::numeric_limits<double>::quiet_NaN());
            ImGui::TableSetColumnIndex(4);
            drawValue(in_band, stage_point ? evaluation->metrics.oip3_dBm[m_cursor_index]
                                           : std::numeric_limits<double>::quiet_NaN());
            ImGui::TableSetColumnIndex(5);
            drawValue(in_band, stage_point ? evaluation->metrics.p1db_dBm[m_cursor_index]
                                           : std::numeric_limits<double>::quiet_NaN());

            bool result_point = have_point && m_cursor_index < m_budget_result.valid.size() &&
                                m_budget_result.valid[m_cursor_index] &&
                                m_cursor_index < m_budget_result.gain_dB.size() &&
                                m_cursor_index < m_budget_result.nf_dB.size() &&
                                m_cursor_index < m_budget_result.iip3_dBm.size() &&
                                m_cursor_index < m_budget_result.p1db_dBm.size();

            const double nan = std::numeric_limits<double>::quiet_NaN();
            ImGui::TableSetColumnIndex(6);
            drawValue(result_point, result_point ? m_budget_result.gain_dB[m_cursor_index] : nan);
            ImGui::TableSetColumnIndex(7);
            drawValue(result_point, result_point ? m_budget_result.nf_dB[m_cursor_index] : nan);
            ImGui::TableSetColumnIndex(8);
            drawValue(result_point, result_point ? m_budget_result.iip3_dBm[m_cursor_index] : nan);
            ImGui::TableSetColumnIndex(9);
            drawValue(result_point, result_point ? m_budget_result.p1db_dBm[m_cursor_index] : nan);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (budget.stages.empty())
        ImGui::TextDisabled("Add a library part");

    ImGui::End();
}
