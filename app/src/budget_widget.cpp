#include "budget_widget.h"

#include "imgui.h"
#include "implot.h"
#include "receiver_requirements_widget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kBudgetColumnCount = 10;
constexpr const char *kUnavailable = "--";

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

// Same inputs as the evaluator's cache key: any stage snapshot, grid, stimulus, or library
// root-set change invalidates both the per-stage evaluation and the cascade result.
std::string resultKey(const BudgetConfig &config, const std::vector<double> &frequencies_Hz,
                      double stimulus_power_dBm, std::size_t root_generation) {
    std::ostringstream key;
    key << std::setprecision(17);
    key << budgetToJson(config).dump() << '|' << stimulus_power_dBm << '|' << root_generation
        << '|';
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

const char *statusText(ReceiverRequirementStatus status) {
    switch (status) {
    case ReceiverRequirementStatus::NotConfigured:
        return "Not configured";
    case ReceiverRequirementStatus::InvalidConfiguration:
        return "Invalid configuration";
    case ReceiverRequirementStatus::Incomplete:
        return "Incomplete";
    case ReceiverRequirementStatus::Pass:
        return "Pass";
    case ReceiverRequirementStatus::Fail:
        return "Fail";
    }
    return "Incomplete";
}

void drawStatusText(const char *label, ReceiverRequirementStatus status) {
    switch (receiverRequirementStatusTone(status)) {
    case ReceiverRequirementStatusTone::PassGreen:
        ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.35f, 1.0f), "%s: %s", label, statusText(status));
        break;
    case ReceiverRequirementStatusTone::FailRed:
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s: %s", label, statusText(status));
        break;
    case ReceiverRequirementStatusTone::Neutral:
        ImGui::TextDisabled("%s: %s", label, statusText(status));
        break;
    }
}

std::vector<double> toGHz(const std::vector<double> &frequencies_Hz) {
    std::vector<double> frequencies_GHz;
    frequencies_GHz.reserve(frequencies_Hz.size());
    for (const double frequency : frequencies_Hz)
        frequencies_GHz.push_back(frequency / 1.0e9);
    return frequencies_GHz;
}

// Drops every sample after the first `on` in each `on + off` run by turning it into NaN.
// Drawn with ImPlotLineFlags_SkipNaN this makes a solid series dashed (on > 1) or dotted
// (on == 1); a single surviving sample between gaps draws no segment of its own.
std::vector<double> dashPattern(const std::vector<double> &values, int on, int off) {
    std::vector<double> pattern = values;
    const int period = on + off;
    if (period <= 0)
        return pattern;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (static_cast<int>(i % static_cast<std::size_t>(period)) >= on)
            pattern[i] = std::numeric_limits<double>::quiet_NaN();
    }
    return pattern;
}

void plotSeries(const char *label, const std::vector<double> &frequencies_GHz,
                const std::vector<double> &values, const ImVec4 &color, int on, int off) {
    const std::size_t count = std::min(frequencies_GHz.size(), values.size());
    if (count == 0)
        return;
    std::vector<double> plotted(values.begin(),
                                values.begin() + static_cast<std::ptrdiff_t>(count));
    // computeBudget emits +inf for a cascade with no nonlinearity. ImPlot's line renderer
    // only guards NaN around PrimLine, so every non-finite point is normalized here before
    // dashing/plotting; the SkipNaN flag then breaks the line instead of drawing an inf.
    for (double &value : plotted) {
        if (!std::isfinite(value))
            value = std::numeric_limits<double>::quiet_NaN();
    }
    if (on > 0)
        plotted = dashPattern(plotted, on, off);
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = 2.0f;
    spec.Flags = ImPlotLineFlags_SkipNaN;
    ImPlot::PlotLine(label, frequencies_GHz.data(), plotted.data(), static_cast<int>(count), spec);
}

// A horizontal requirement limit drawn as a dashed segment over the band only.
void plotLimit(const char *label, const ReceiverRequirementsConfig &config, double value) {
    if (!std::isfinite(value) || !(config.band_stop_Hz > config.band_start_Hz))
        return;
    constexpr int kSamples = 129;
    std::vector<double> frequencies_GHz(kSamples);
    std::vector<double> values(kSamples, value);
    for (int i = 0; i < kSamples; ++i) {
        const double fraction = static_cast<double>(i) / static_cast<double>(kSamples - 1);
        frequencies_GHz[i] =
            (config.band_start_Hz + (config.band_stop_Hz - config.band_start_Hz) * fraction) /
            1.0e9;
    }
    const std::vector<double> dashed = dashPattern(values, 6, 4);
    ImPlotSpec spec;
    spec.LineColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    spec.LineWeight = 1.0f;
    spec.Flags = ImPlotItemFlags_NoLegend | static_cast<int>(ImPlotLineFlags_SkipNaN);
    ImPlot::PlotLine(label, frequencies_GHz.data(), dashed.data(), kSamples, spec);
}

void plotCursor(double cursor_Hz) {
    if (!std::isfinite(cursor_Hz))
        return;
    const double cursor_GHz = cursor_Hz / 1.0e9;
    ImPlotSpec spec;
    spec.LineColor = ImVec4(0.95f, 0.85f, 0.35f, 1.0f);
    spec.LineWeight = 1.0f;
    spec.Flags = ImPlotItemFlags_NoLegend;
    ImPlot::PlotInfLines("Cursor", &cursor_GHz, 1, spec);
}

// One result plot: requirement limits, the predicted cascade, the live simulation, and the
// shared cursor. The budget curve is always solid and the simulation curve short-dashed.
void drawBudgetPlot(
    const char *id, const char *title, const char *unit, const std::vector<double> &frequencies_Hz,
    const std::vector<double> &budget_values, const std::vector<double> &simulation_values,
    const ReceiverRequirementsState &requirements, const ReceiverMetricEvaluation &metric,
    const ReceiverMetricEvaluation &simulation_metric, std::optional<double> limit_min,
    std::optional<double> limit_max, double cursor_Hz) {
    ImGui::PushID(id);

    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    if (!requirements.config) {
        ImGui::TextDisabled("Configure receiver requirements");
    } else {
        drawStatusText("Budget", metric.status);
        ImGui::SameLine(0.0f, 16.0f);
        drawStatusText("Simulation", simulation_metric.status);
    }

    if (frequencies_Hz.empty()) {
        ImGui::TextDisabled("Simulation unavailable");
        ImGui::PopID();
        return;
    }
    if (simulation_values.empty())
        ImGui::TextDisabled("Simulation unavailable");

    const std::vector<double> frequencies_GHz = toGHz(frequencies_Hz);
    if (ImPlot::BeginPlot(id, ImVec2(-1.0f, 180.0f), ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("Frequency (GHz)", unit);
        if (requirements.config) {
            if (limit_min)
                plotLimit("##limit_min", *requirements.config, *limit_min);
            if (limit_max)
                plotLimit("##limit_max", *requirements.config, *limit_max);
        }
        plotSeries("Budget", frequencies_GHz, budget_values, ImVec4(0.35f, 0.65f, 1.0f, 1.0f), 0,
                   0);
        plotSeries("Simulation", frequencies_GHz, simulation_values,
                   ImVec4(0.35f, 0.9f, 0.55f, 1.0f), 2, 3);
        plotCursor(cursor_Hz);
        ImPlot::EndPlot();
    }
    ImGui::PopID();
}

} // namespace

bool BudgetWidget::addStage(BudgetConfig &budget, const ComponentDefinition &definition) {
    // While an unsupported-version section is preserved verbatim, saving writes that section
    // and nothing else, so an edit here would be silently discarded. Refuse instead.
    if (!budget.unsupported_section.is_null())
        return false;
    const std::string stage_type = budgetNormalizeStageType(definition.type);
    if (stage_type.empty())
        return false;
    BudgetStage stage = stageFromDefinition(definition);
    // The library and evaluator use the registry name for the ideal filter.
    stage.type = stage_type;
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
    const auto last = static_cast<long long>(budget.stages.size()) - 1;
    const long long target = static_cast<long long>(index) + delta;
    if (target < 0 || target > last)
        return false;

    auto first = budget.stages.begin() + static_cast<std::ptrdiff_t>(index);
    auto destination = budget.stages.begin() + static_cast<std::ptrdiff_t>(target);
    if (delta > 0)
        std::rotate(first, first + 1, destination + 1);
    else
        std::rotate(destination, first, first + 1);

    // The selection follows the stage that moved, and the stages it jumped over shift one
    // slot in the opposite direction.
    const int from = static_cast<int>(index);
    const int to = static_cast<int>(target);
    if (m_selected_stage == from) {
        m_selected_stage = to;
    } else if (delta > 0 && m_selected_stage > from && m_selected_stage <= to) {
        --m_selected_stage;
    } else if (delta < 0 && m_selected_stage >= to && m_selected_stage < from) {
        ++m_selected_stage;
    }
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

std::vector<BudgetResult>
BudgetWidget::rowCascades(const BudgetConfig &budget, const std::vector<double> &frequencies_Hz,
                          const std::vector<BudgetStageMetrics> &stage_metrics) const {
    std::vector<BudgetResult> rows;
    rows.reserve(budget.stages.size());
    BudgetConfig prefix = budget;
    prefix.stages.clear();
    std::vector<BudgetStageMetrics> prefix_metrics;
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        prefix.stages.push_back(budget.stages[i]);
        if (i < stage_metrics.size())
            prefix_metrics.push_back(stage_metrics[i]);
        rows.push_back(computeBudget(prefix, frequencies_Hz, prefix_metrics));
    }
    return rows;
}

double BudgetWidget::outputReferredOip3_dBm(const BudgetResult &cascade, std::size_t point) {
    if (point >= cascade.iip3_dBm.size() || point >= cascade.gain_dB.size())
        return std::numeric_limits<double>::quiet_NaN();
    // inf + finite stays inf; any NaN input propagates as the placeholder.
    return cascade.iip3_dBm[point] + cascade.gain_dB[point];
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
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    if (m_frequencies_Hz != frequencies_Hz)
        m_frequencies_Hz = frequencies_Hz;

    m_simulation_gain_dB = simulation_gain_dB;
    m_simulation_nf_dB = simulation_nf_dB;
    m_simulation_output_power_dBm = simulation_output_power_dBm;
    m_simulation_iip3_dBm = simulation_iip3_dBm;
    m_simulation_result = simulation_result;

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
    // An unsupported-version section is written back verbatim, so edits would be discarded.
    const bool budget_read_only = !budget.unsupported_section.is_null();

    ImGui::BeginDisabled(budget_read_only);
    if (ImGui::Button("Add part"))
        ImGui::OpenPopup("##budget_add_part");
    ImGui::EndDisabled();
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
    ImGui::BeginDisabled(budget_read_only);
    double input_power = budget.input_power_dBm;
    if (ImGui::InputDouble("Input power (dBm)", &input_power, 0.0, 0.0, "%.2f") &&
        setBudgetInputPower(budget, input_power) && onChange)
        onChange();
    ImGui::EndDisabled();

    // The budget predicts output power from the input power above, while the simulation uses
    // the Network Analyzer's source-tone power. Showing it makes a systematic offset visible.
    ImGui::SameLine();
    ImGui::TextDisabled("Sim source tone: %.2f dBm", stimulus_power_dBm);

    // An unsupported-version section was preserved verbatim; the stages below are not its
    // stages, so say so rather than presenting an empty cascade as the user's budget.
    if (!budget.unsupported_reason.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextUnformatted(budget.unsupported_reason.c_str());
        ImGui::PopStyleColor();
    }

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
                if (!definition || !budgetSupportsStageType(definition->type))
                    continue;

                std::string haystack = definition->part_number + " " + definition->manufacturer +
                                       " " + definition->description + " " + definition->type;
                if (!needle.empty() && lowerCopy(haystack).find(needle) == std::string::npos)
                    continue;

                std::string label;
                if (!definition->data_files.empty())
                    label = "[DATA] ";
                label += definition->part_number;
                if (!definition->manufacturer.empty())
                    label += " -- " + definition->manufacturer;
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
    const std::string key =
        resultKey(budget, frequencies_Hz, stimulus_power_dBm, library.rootGeneration());
    if (key != m_result_key) {
        m_evaluations = m_evaluator.evaluate(budget, library, frequencies_Hz, stimulus_power_dBm);
        std::vector<BudgetStageMetrics> stage_metrics;
        stage_metrics.reserve(m_evaluations.size());
        for (const BudgetStageEvaluation &evaluation : m_evaluations)
            stage_metrics.push_back(evaluation.metrics);
        m_row_cascades = rowCascades(budget, frequencies_Hz, stage_metrics);
        m_budget_result = computeBudget(budget, frequencies_Hz, stage_metrics);
        m_result_key = key;
    }

    // The predicted cascade is evaluated with the same rules the live simulation uses. An
    // empty budget has no curve of its own, so it stays Incomplete rather than passing on the
    // zero-gain result computeBudget would return.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (budget.stages.empty()) {
        m_budget_evaluation = ReceiverRequirementsEvaluation{};
    } else {
        const double sweep_start = frequencies_Hz.empty() ? nan : frequencies_Hz.front();
        const double sweep_stop = frequencies_Hz.empty() ? nan : frequencies_Hz.back();
        m_budget_evaluation = evaluateReceiverRequirements(
            requirements, sweep_start, sweep_stop, frequencies_Hz, m_budget_result.gain_dB,
            m_budget_result.nf_dB, m_budget_result.output_power_dBm, m_budget_result.iip3_dBm);
    }

    const bool have_point = !frequencies_Hz.empty() && m_cursor_index < frequencies_Hz.size();

    // --- Overall status ----------------------------------------------------------
    drawStatusText("Budget", m_budget_evaluation.overall);
    ImGui::SameLine(0.0f, 16.0f);
    drawStatusText("Simulation", m_simulation_result.overall);

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
                    label += " -- " + evaluation->diagnostic;
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

            const BudgetResult *cascade = i < m_row_cascades.size() ? &m_row_cascades[i] : nullptr;
            bool result_point =
                have_point && cascade != nullptr && m_cursor_index < cascade->valid.size() &&
                cascade->valid[m_cursor_index] && m_cursor_index < cascade->gain_dB.size() &&
                m_cursor_index < cascade->nf_dB.size() &&
                m_cursor_index < cascade->iip3_dBm.size() &&
                m_cursor_index < cascade->p1db_dBm.size();

            const double nan = std::numeric_limits<double>::quiet_NaN();
            ImGui::TableSetColumnIndex(6);
            drawValue(result_point, result_point ? cascade->gain_dB[m_cursor_index] : nan);
            ImGui::TableSetColumnIndex(7);
            drawValue(result_point, result_point ? cascade->nf_dB[m_cursor_index] : nan);
            ImGui::TableSetColumnIndex(8);
            drawValue(result_point,
                      result_point ? outputReferredOip3_dBm(*cascade, m_cursor_index) : nan);
            ImGui::TableSetColumnIndex(9);
            drawValue(result_point, result_point ? cascade->p1db_dBm[m_cursor_index] : nan);

            ImGui::PopID();
        }

        // A stage entry the loader could not interpret is still shown, in red, with the
        // reason, and is written back unchanged on save. It is not part of the cascade.
        for (std::size_t i = 0; i < budget.rejected_stages.size(); ++i) {
            const BudgetRejectedStage &rejected = budget.rejected_stages[i];
            ImGui::PushID(static_cast<int>(budget.stages.size() + i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kUnavailable);
            ImGui::TableSetColumnIndex(1);
            std::string label = "Unresolved stage";
            if (rejected.raw.is_object()) {
                const auto part = rejected.raw.find("part_number");
                if (part != rejected.raw.end() && part->is_string() && !part->empty())
                    label = part->get<std::string>();
            }
            label += " -- " + rejected.reason;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (budget.stages.empty() && budget.rejected_stages.empty())
        ImGui::TextDisabled("Add a library part");

    // --- Result plots ------------------------------------------------------------
    if (!budget.stages.empty()) {
        ImGui::SeparatorText("Predicted performance");

        std::optional<double> gain_min;
        std::optional<double> gain_max;
        std::optional<double> nf_max;
        std::optional<double> power_min;
        std::optional<double> power_max;
        std::optional<double> iip3_min;
        if (requirements.config) {
            const ReceiverRequirementsConfig &config = *requirements.config;
            if (config.gain) {
                gain_min = config.gain->minimum_dB;
                gain_max = config.gain->maximum_dB;
            }
            nf_max = config.nf_max_dB;
            if (config.output_power) {
                power_min = config.output_power->minimum_dBm;
                power_max = config.output_power->maximum_dBm;
            }
            iip3_min = config.iip3_min_dBm;
        }

        const ImGuiTableFlags plot_table_flags = ImGuiTableFlags_SizingStretchSame;
        if (ImGui::BeginTable("BudgetPlots", 2, plot_table_flags)) {
            ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            drawBudgetPlot("gain", "Gain (dB)", "Gain (dB)", frequencies_Hz,
                           m_budget_result.gain_dB, m_simulation_gain_dB, requirements,
                           m_budget_evaluation.gain, m_simulation_result.gain, gain_min, gain_max,
                           m_cursor_Hz);
            ImGui::TableNextColumn();
            drawBudgetPlot("nf", "Noise figure (dB)", "NF (dB)", frequencies_Hz,
                           m_budget_result.nf_dB, m_simulation_nf_dB, requirements,
                           m_budget_evaluation.noise_figure, m_simulation_result.noise_figure,
                           std::nullopt, nf_max, m_cursor_Hz);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawBudgetPlot("output_power", "Output power (dBm)", "Output power (dBm)",
                           frequencies_Hz, m_budget_result.output_power_dBm,
                           m_simulation_output_power_dBm, requirements,
                           m_budget_evaluation.output_power, m_simulation_result.output_power,
                           power_min, power_max, m_cursor_Hz);
            ImGui::TableNextColumn();
            drawBudgetPlot("iip3", "IIP3 (dBm)", "IIP3 (dBm)", frequencies_Hz,
                           m_budget_result.iip3_dBm, m_simulation_iip3_dBm, requirements,
                           m_budget_evaluation.iip3, m_simulation_result.iip3, iip3_min,
                           std::nullopt, m_cursor_Hz);
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

ReceiverRequirementStatus BudgetWidget::budgetStatus() const { return m_budget_evaluation.overall; }

ReceiverRequirementStatus BudgetWidget::simulationStatus() const {
    return m_simulation_result.overall;
}

const ReceiverRequirementsEvaluation &BudgetWidget::budgetEvaluation() const {
    return m_budget_evaluation;
}
