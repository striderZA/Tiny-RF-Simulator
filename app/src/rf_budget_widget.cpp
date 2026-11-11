#include "rf_budget_widget.h"

#include "component_library.h"
#include "component_type_registry.h"
#include "imgui.h"
#include "implot.h"
#include "rf_view_ui.h"
#include "utils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kRenameStageTitle = "Rename stage";
constexpr const char *kRenameOutputTitle = "Rename output";
constexpr const char *kFeedsTitle = "Stage feeds";
constexpr const char *kParamsTitle = "Stage parameters";

const RfBudgetStageType *stageTypeInfo(std::string_view type) {
    for (const auto &entry : rfBudgetStageTypes()) {
        if (entry.type == type)
            return &entry;
    }
    return nullptr;
}

const ComponentTypeDescriptor *descriptorFor(std::string_view type) {
    return ComponentTypeRegistry::instance().find(type);
}

// The registry's display name, so the stage table reads "Amplifier" rather than "amplifier".
std::string stageTypeLabel(std::string_view type) {
    const ComponentTypeDescriptor *descriptor = descriptorFor(type);
    if (descriptor && !descriptor->display_name.empty())
        return descriptor->display_name;
    return std::string(type);
}

// Fields a stage's params editor offers. Data-file paths and read-only metadata are project
// state, not budget settings; the budget refuses them (applyComponentParams returns
// PathParamUnsupported).
std::vector<const ParameterField *> editableFields(std::string_view type) {
    std::vector<const ParameterField *> fields;
    const ComponentTypeDescriptor *descriptor = descriptorFor(type);
    if (!descriptor)
        return fields;
    for (const ParameterField &field : descriptor->state_fields) {
        if (field.read_only || field.kind == FieldKind::FilePath)
            continue;
        // A budget stage reads an idealized model, never a data file; the S-parameter
        // switches would only make the stage refuse to build.
        if (field.key == "sparam_mode" || field.key == "sparam_fwd_idx")
            continue;
        if (field.kind == FieldKind::Number || field.kind == FieldKind::Enum ||
            field.kind == FieldKind::Bool)
            fields.push_back(&field);
    }
    return fields;
}

// Numeric mode overrides: any number, and an enum as its stored index. This is how the switch
// throw is overridden per mode.
std::vector<const ParameterField *> overrideFields(std::string_view type) {
    std::vector<const ParameterField *> fields;
    for (const ParameterField *field : editableFields(type)) {
        if (field->kind == FieldKind::Number || field->kind == FieldKind::Enum)
            fields.push_back(field);
    }
    return fields;
}

const ParameterField *findField(std::string_view type, std::string_view key) {
    for (const ParameterField *field : editableFields(type)) {
        if (field->key == key)
            return field;
    }
    return nullptr;
}

bool paramsHas(const RfBudgetStage &stage, const std::string &key) {
    return stage.params.contains(key) && !stage.params[key].is_null();
}

// A mode combo entry says whether the mode has a budget record, so the difference between
// "no overrides" and "not budgeted" is visible before opening the Requirements view.
std::string modeLabel(const ReceiverRequirementsSet &requirements, const RfBudget &budget,
                      std::size_t index) {
    std::string label = requirements.modes[index].name;
    if (findRfBudgetModeRecord(budget, requirements.modes[index].id))
        label += " (budgeted)";
    return label;
}

std::optional<RfBudgetFeed> feedFromOption(int option, const RfBudget &budget,
                                           std::size_t self_index) {
    if (option == 0)
        return std::nullopt;
    if (option == 1)
        return RfBudgetFeed{std::nullopt, 0};
    int cursor = 2;
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        if (i == self_index)
            continue;
        const RfBudgetStageType *info = stageTypeInfo(budget.stages[i].type);
        const int outputs = info ? info->outputs : 0;
        for (int port = 0; port < outputs; ++port) {
            if (cursor++ == option)
                return RfBudgetFeed{budget.stages[i].name, port};
        }
    }
    return std::nullopt;
}

std::vector<std::string> feedOptions(const RfBudget &budget, std::size_t self_index) {
    std::vector<std::string> options{"Unfed", "Budget input"};
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        if (i == self_index)
            continue;
        const RfBudgetStageType *info = stageTypeInfo(budget.stages[i].type);
        const int outputs = info ? info->outputs : 0;
        for (int port = 0; port < outputs; ++port) {
            options.push_back(budget.stages[i].name + " out " + std::to_string(port));
        }
    }
    return options;
}

int feedOptionIndex(const RfBudget &budget, std::size_t self_index,
                    const std::optional<RfBudgetFeed> &feed) {
    if (!feed)
        return 0;
    if (!feed->stage)
        return 1;
    int cursor = 2;
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        if (i == self_index)
            continue;
        const RfBudgetStageType *info = stageTypeInfo(budget.stages[i].type);
        const int outputs = info ? info->outputs : 0;
        for (int port = 0; port < outputs; ++port) {
            if (budget.stages[i].name == *feed->stage && port == feed->port)
                return cursor;
            ++cursor;
        }
    }
    return 0;
}

// One cascade cell. A NaN point is blank and carries a reason; an infinity is a real value
// (no transmission, linear OIP3, infinite headroom) and carries its reason too.
void drawSeriesCell(const RfBudgetSeries &series, std::size_t point,
                    const RfBudgetReasonTable &reasons) {
    if (point >= series.values.size()) {
        ImGui::TextDisabled("-");
        return;
    }
    const double value = series.values[point];
    const std::uint16_t reason_index = point < series.reasons.size() ? series.reasons[point] : 0;
    const std::string &reason = reasons.at(reason_index);
    if (std::isnan(value)) {
        ImGui::TextDisabled("-");
    } else {
        char text[32];
        rf_view_ui::formatSeriesValue(value, text, sizeof(text));
        ImGui::TextUnformatted(text);
    }
    if (!reason.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", reason.c_str());
}

// The same cell over the whole band: "low .. high" of the finite points, blank when none.
void drawSeriesRangeCell(const RfBudgetSeries &series, const RfBudgetReasonTable &reasons) {
    double low = 0.0;
    double high = 0.0;
    std::size_t high_index = 0;
    bool any = false;
    for (std::size_t index = 0; index < series.values.size(); ++index) {
        const double value = series.values[index];
        if (std::isnan(value))
            continue;
        if (!any) {
            low = high = value;
            high_index = index;
            any = true;
            continue;
        }
        if (value < low)
            low = value;
        if (value > high) {
            high = value;
            high_index = index;
        }
    }
    if (!any) {
        ImGui::TextDisabled("-");
        return;
    }
    char low_text[32];
    char high_text[32];
    rf_view_ui::formatSeriesValue(low, low_text, sizeof(low_text));
    rf_view_ui::formatSeriesValue(high, high_text, sizeof(high_text));
    if (low == high)
        ImGui::TextUnformatted(low_text);
    else
        ImGui::Text("%s .. %s", low_text, high_text);
    const std::uint16_t reason_index =
        high_index < series.reasons.size() ? series.reasons[high_index] : 0;
    const std::string &reason = reasons.at(reason_index);
    if (!reason.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", reason.c_str());
}

} // namespace

void RfBudgetWidget::markChanged() {
    if (onChange)
        onChange();
}

void RfBudgetWidget::openRenameStage(std::size_t index, const RfBudget &budget) {
    if (index >= budget.stages.size())
        return;
    m_modal = Modal::RenameStage;
    m_modal_stage = index;
    std::snprintf(m_name_buffer.data(), m_name_buffer.size(), "%s",
                  budget.stages[index].name.c_str());
    m_error.clear();
}

void RfBudgetWidget::openRenameOutput(std::size_t index, const RfBudget &budget) {
    if (index >= budget.outputs.size())
        return;
    m_modal = Modal::RenameOutput;
    m_modal_output = index;
    std::snprintf(m_name_buffer.data(), m_name_buffer.size(), "%s",
                  budget.outputs[index].name.c_str());
    m_error.clear();
}

void RfBudgetWidget::draw(const char *title, bool *p_open, RfBudget &budget,
                          const ReceiverRequirementsSet &requirements,
                          const ComponentLibrary &library, const RfBudgetEvaluation &evaluation) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    // The Budget view opens on the receiver mode selected in the Requirements view, and follows
    // that selection. Choosing another mode here edits that mode's record without changing the
    // Requirements view's selection.
    const std::size_t selected =
        requirements.selected < requirements.modes.size() ? requirements.selected : 0;
    if (!m_initialized) {
        m_override_mode = selected;
        m_cascade_mode = selected;
        m_initialized = true;
    } else if (requirements.selected != m_last_requirements_selected) {
        m_override_mode = selected;
        m_cascade_mode = selected;
        m_override_stage = 0;
        m_override_key = 0;
        m_cascade_output = 0;
        m_cascade_point = 0;
    }
    m_last_requirements_selected = requirements.selected;

    drawToolbar(budget);
    drawStatus(budget, evaluation);
    drawStages(budget, library);
    drawOutputs(budget);
    drawOverrides(budget, requirements);
    drawCascade(budget, requirements, evaluation);
    drawModals(budget);
    ImGui::End();
}

void RfBudgetWidget::drawToolbar(RfBudget &budget) {
    if (ImGui::Button("Add Stage")) {
        std::string error;
        if (addRfBudgetStage(budget, "amplifier", error)) {
            m_error.clear();
            markChanged();
        } else {
            m_error = std::move(error);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(budget.stages.empty());
    if (ImGui::Button("Add Output")) {
        std::string error;
        if (addRfBudgetOutput(budget, 0, 0, error)) {
            m_error.clear();
            markChanged();
        } else {
            m_error = std::move(error);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        if (onRefresh)
            onRefresh();
    }
    ImGui::SameLine();
    int points = budget.point_count;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::InputInt("Points", &points)) {
        points = std::clamp(points, kMinRfBudgetPoints, kMaxRfBudgetPoints);
        if (points != budget.point_count) {
            budget.point_count = points;
            markChanged();
        }
    }
    // A refusal takes its own line below the toolbar: it is a sentence, so it wraps rather than
    // clipping at the window edge. Nothing below the toolbar was under the pointer when the
    // refusal was produced.
    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rf_view_ui::failColor());
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
    }
}

void RfBudgetWidget::drawStatus(RfBudget &budget, const RfBudgetEvaluation &evaluation) {
    // A retained invalid marker is a decoded shape failure with no table behind it. Show it
    // before the empty hint and offer the only way out: discard the marker.
    if (!budget.invalid_reason.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rf_view_ui::failColor());
        ImGui::TextWrapped("%s", budget.invalid_reason.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Discard invalid budget")) {
            budget = RfBudget{};
            m_error.clear();
            markChanged();
        }
        return;
    }
    // An empty budget is not an error: a fresh project opens the view with the hint below.
    if (budget.empty()) {
        rf_view_ui::hint("No stages yet. Add a stage to describe the chain, then add an output "
                         "and budget a requirement mode.");
        return;
    }
    if (auto reason = validateRfBudget(budget)) {
        ImGui::PushStyleColor(ImGuiCol_Text, rf_view_ui::failColor());
        ImGui::TextWrapped("%s", reason->c_str());
        ImGui::PopStyleColor();
        return;
    }
    if (!evaluation.not_evaluated_reason.empty()) {
        rf_view_ui::hint(evaluation.not_evaluated_reason.c_str());
    } else {
        ImGui::TextDisabled("%zu stage(s), %zu output(s), %zu mode(s) evaluated",
                            budget.stages.size(), budget.outputs.size(), evaluation.modes.size());
    }
    if (!evaluation.orphaned_mode_ids.empty())
        ImGui::TextColored(rf_view_ui::warnColor(),
                           "%zu budget record(s) name a mode that no longer exists.",
                           evaluation.orphaned_mode_ids.size());
}

void RfBudgetWidget::drawStages(RfBudget &budget, const ComponentLibrary &library) {
    rf_view_ui::sectionHeader("Stages");
    if (budget.stages.empty()) {
        rf_view_ui::hint("Add Stage appends an amplifier. Choose its type, source, and feeds "
                         "here, and give it a name you recognize.");
        return;
    }
    if (!ImGui::BeginTable("budget_stages", 6,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Part / Params", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Feeds", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableHeadersRow();

    for (std::size_t index = 0; index < budget.stages.size(); ++index) {
        RfBudgetStage &stage = budget.stages[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(stage.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", stage.name.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##type", stageTypeLabel(stage.type).c_str())) {
            for (const RfBudgetStageType &type : rfBudgetStageTypes()) {
                const bool selected = type.type == stage.type;
                if (ImGui::Selectable(stageTypeLabel(type.type).c_str(), selected) && !selected) {
                    setRfBudgetStageType(budget, index, type.type);
                    markChanged();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char *source_preview =
            stage.source == RfBudgetStageSource::Library ? "Library" : "Idealized";
        if (ImGui::BeginCombo("##source", source_preview)) {
            if (ImGui::Selectable("Idealized", stage.source == RfBudgetStageSource::Idealized)) {
                if (stage.source != RfBudgetStageSource::Idealized) {
                    stage.source = RfBudgetStageSource::Idealized;
                    stage.part_number.clear();
                    markChanged();
                }
            }
            if (ImGui::Selectable("Library", stage.source == RfBudgetStageSource::Library)) {
                if (stage.source != RfBudgetStageSource::Library) {
                    stage.source = RfBudgetStageSource::Library;
                    stage.params = nlohmann::ordered_json::object();
                    markChanged();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextColumn();
        if (stage.source == RfBudgetStageSource::Library) {
            const auto parts = library.byType(stage.type);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const char *part_preview =
                stage.part_number.empty() ? "(choose part)" : stage.part_number.c_str();
            if (ImGui::BeginCombo("##part", part_preview)) {
                for (const ComponentDefinition *part : parts) {
                    if (!part)
                        continue;
                    const bool selected = part->part_number == stage.part_number;
                    if (ImGui::Selectable(part->part_number.c_str(), selected) && !selected) {
                        stage.part_number = part->part_number;
                        markChanged();
                    }
                }
                ImGui::EndCombo();
            }
            if (parts.empty())
                ImGui::TextDisabled("No %s parts in the library.",
                                    stageTypeLabel(stage.type).c_str());
        } else if (ImGui::Button("Params...")) {
            m_modal = Modal::StageParams;
            m_modal_stage = index;
            m_error.clear();
        }

        ImGui::TableNextColumn();
        if (stage.inputs.empty()) {
            ImGui::TextDisabled("no inputs");
        }
        for (std::size_t port = 0; port < stage.inputs.size(); ++port) {
            const std::optional<RfBudgetFeed> &feed = stage.inputs[port];
            ImGui::TextDisabled("in %zu", port);
            ImGui::SameLine();
            if (!feed)
                ImGui::TextColored(rf_view_ui::warnColor(), "unfed");
            else if (!feed->stage)
                ImGui::TextUnformatted("budget input");
            else
                ImGui::Text("%s out %d", feed->stage->c_str(), feed->port);
        }
        if (ImGui::SmallButton("Feeds...")) {
            m_modal = Modal::StageFeeds;
            m_modal_stage = index;
            m_error.clear();
        }

        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Rename..."))
            openRenameStage(index, budget);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            if (removeRfBudgetStage(budget, index)) {
                m_error.clear();
                markChanged();
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void RfBudgetWidget::drawOutputs(RfBudget &budget) {
    rf_view_ui::sectionHeader("Outputs");
    if (budget.outputs.empty()) {
        rf_view_ui::hint("No outputs. An output names a stage port, and each reached output gets "
                         "its own cascade and verdict.");
        return;
    }
    if (!ImGui::BeginTable("budget_outputs", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableHeadersRow();

    for (std::size_t index = 0; index < budget.outputs.size(); ++index) {
        RfBudgetOutput &output = budget.outputs[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(output.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", output.name.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const char *stage_preview = output.stage.empty() ? "(choose stage)" : output.stage.c_str();
        if (ImGui::BeginCombo("##stage", stage_preview)) {
            for (const RfBudgetStage &stage : budget.stages) {
                const bool selected = stage.name == output.stage;
                if (ImGui::Selectable(stage.name.c_str(), selected) && !selected) {
                    output.stage = stage.name;
                    markChanged();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextColumn();
        const RfBudgetStage *stage = nullptr;
        for (const RfBudgetStage &candidate : budget.stages) {
            if (candidate.name == output.stage) {
                stage = &candidate;
                break;
            }
        }
        const RfBudgetStageType *info = stage ? stageTypeInfo(stage->type) : nullptr;
        const int ports = info ? info->outputs : 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##port", std::to_string(output.port).c_str())) {
            for (int port = 0; port < ports; ++port) {
                const bool selected = port == output.port;
                if (ImGui::Selectable(std::to_string(port).c_str(), selected) && !selected) {
                    output.port = port;
                    markChanged();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Rename..."))
            openRenameOutput(index, budget);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            if (removeRfBudgetOutput(budget, index)) {
                markChanged();
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void RfBudgetWidget::drawOverrides(RfBudget &budget, const ReceiverRequirementsSet &requirements) {
    rf_view_ui::sectionHeader("Mode overrides");
    if (requirements.modes.empty()) {
        rf_view_ui::hint("No requirement modes. Add a mode in Receiver Requirements first.");
        return;
    }
    m_override_mode = std::min(m_override_mode, requirements.modes.size() - 1);
    ImGui::SetNextItemWidth(240.0f);
    const std::string override_preview = modeLabel(requirements, budget, m_override_mode);
    if (ImGui::BeginCombo("Mode##override_mode", override_preview.c_str())) {
        for (std::size_t index = 0; index < requirements.modes.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const bool selected = index == m_override_mode;
            const std::string label = modeLabel(requirements, budget, index);
            if (ImGui::Selectable(label.c_str(), selected) && !selected) {
                m_override_mode = index;
                m_override_stage = 0;
                m_override_key = 0;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const std::uint32_t mode_id = requirements.modes[m_override_mode].id;
    RfBudgetModeRecord *record = findRfBudgetModeRecord(budget, mode_id);
    bool clear_record = false;
    if (record) {
        rf_view_ui::tag("Budgeted", rf_view_ui::passColor(), true);
        ImGui::BeginDisabled(!record->overrides.empty());
        if (ImGui::Button("Clear budget record"))
            clear_record = true;
        ImGui::EndDisabled();
        if (!record->overrides.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Remove the overrides first.");
    } else {
        rf_view_ui::tag("Not budgeted", rf_view_ui::warnColor(), true);
        if (ImGui::Button("Budget this mode")) {
            record = &ensureRfBudgetModeRecord(budget, mode_id);
            markChanged();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("A mode is budgeted only after it has a record.");
    }
    if (clear_record) {
        eraseRfBudgetModeRecord(budget, mode_id);
        record = nullptr;
        markChanged();
    }
    if (record &&
        ImGui::BeginTable("budget_overrides", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        std::optional<std::pair<std::string, std::string>> erase;
        for (auto &[stage_name, overrides] : record->overrides) {
            for (auto &[key, value] : overrides) {
                const std::string row_id = stage_name + "##" + key;
                ImGui::PushID(row_id.c_str());
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(stage_name.c_str());

                ImGui::TableNextColumn();
                const RfBudgetStage *stage = nullptr;
                for (const RfBudgetStage &candidate : budget.stages) {
                    if (candidate.name == stage_name) {
                        stage = &candidate;
                        break;
                    }
                }
                const ParameterField *field = stage ? findField(stage->type, key) : nullptr;
                ImGui::TextUnformatted(field ? field->label.c_str() : key.c_str());

                ImGui::TableNextColumn();
                if (field && field->kind == FieldKind::Enum) {
                    const int current = static_cast<int>(value);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const char *preview =
                        current >= 0 &&
                                static_cast<std::size_t>(current) < field->enum_values.size()
                            ? field->enum_values[static_cast<std::size_t>(current)].c_str()
                            : "(index)";
                    if (ImGui::BeginCombo("##enum", preview)) {
                        for (std::size_t option = 0; option < field->enum_values.size(); ++option) {
                            const bool selected = static_cast<int>(option) == current;
                            if (ImGui::Selectable(field->enum_values[option].c_str(), selected) &&
                                !selected) {
                                value = static_cast<double>(option);
                                markChanged();
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    double edited = value;
                    const std::string label = "##value";
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (utils::inputDouble(
                            label, edited, 0.1, 1.0, "%.6g",
                            field ? field->min : -std::numeric_limits<double>::infinity(),
                            field ? field->max : std::numeric_limits<double>::infinity())) {
                        value = edited;
                        markChanged();
                    }
                }

                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Remove"))
                    erase = std::make_pair(stage_name, key);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
        if (erase && eraseRfBudgetOverride(budget, mode_id, erase->first, erase->second))
            markChanged();
    }

    ImGui::Separator();
    if (budget.stages.empty()) {
        rf_view_ui::hint("Add a stage before overriding a setting.");
        return;
    }
    m_override_stage = std::min(m_override_stage, budget.stages.size() - 1);
    ImGui::TextDisabled("One setting of one stage, for this mode only");
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Stage##override_stage", budget.stages[m_override_stage].name.c_str())) {
        for (std::size_t index = 0; index < budget.stages.size(); ++index) {
            const bool selected = index == m_override_stage;
            if (ImGui::Selectable(budget.stages[index].name.c_str(), selected) && !selected) {
                m_override_stage = index;
                m_override_key = 0;
            }
        }
        ImGui::EndCombo();
    }
    const auto fields = overrideFields(budget.stages[m_override_stage].type);
    ImGui::SameLine();
    ImGui::BeginDisabled(fields.empty());
    m_override_key = fields.empty() ? 0 : std::min(m_override_key, fields.size() - 1);
    const char *key_preview =
        fields.empty() ? "(no numeric settings)" : fields[m_override_key]->label.c_str();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Setting##override_key", key_preview)) {
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const bool selected = index == m_override_key;
            if (ImGui::Selectable(fields[index]->label.c_str(), selected) && !selected)
                m_override_key = index;
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool already_overridden = false;
    if (record && !fields.empty()) {
        const auto stage_it = record->overrides.find(budget.stages[m_override_stage].name);
        already_overridden = stage_it != record->overrides.end() &&
                             stage_it->second.contains(fields[m_override_key]->key);
    }
    ImGui::BeginDisabled(fields.empty() || already_overridden);
    if (ImGui::Button("Add override")) {
        const ParameterField &field = *fields[m_override_key];
        double value = 0.0;
        if (paramsHas(budget.stages[m_override_stage], field.key)) {
            const auto &stored = budget.stages[m_override_stage].params[field.key];
            if (stored.is_number())
                value = stored.get<double>();
        }
        // An override the evaluator refuses is useless; the base param may be unset (0.0) or a
        // number outside the field's range.
        if (field.kind == FieldKind::Enum && !field.enum_values.empty()) {
            value = std::clamp(value, 0.0, static_cast<double>(field.enum_values.size() - 1));
        } else {
            value = std::clamp(value, field.min, field.max);
        }
        setRfBudgetOverride(budget, mode_id, budget.stages[m_override_stage].name, field.key,
                            value);
        markChanged();
    }
    ImGui::EndDisabled();
    if (already_overridden && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("This setting already has an override; edit its row above.");
}

void RfBudgetWidget::drawCascade(const RfBudget &budget,
                                 const ReceiverRequirementsSet &requirements,
                                 const RfBudgetEvaluation &evaluation) {
    rf_view_ui::sectionHeader("Cascade");
    if (requirements.modes.empty()) {
        rf_view_ui::hint("No requirement modes.");
        return;
    }
    m_cascade_mode = std::min(m_cascade_mode, requirements.modes.size() - 1);
    ImGui::SetNextItemWidth(240.0f);
    const std::string cascade_preview = modeLabel(requirements, budget, m_cascade_mode);
    if (ImGui::BeginCombo("Mode##cascade_mode", cascade_preview.c_str())) {
        for (std::size_t index = 0; index < requirements.modes.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            const bool selected = index == m_cascade_mode;
            const std::string label = modeLabel(requirements, budget, index);
            if (ImGui::Selectable(label.c_str(), selected) && !selected) {
                m_cascade_mode = index;
                m_cascade_output = 0;
                m_cascade_point = 0;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const RfBudgetModeResult *mode =
        findRfBudgetModeResult(evaluation, requirements.modes[m_cascade_mode].id);
    if (!mode) {
        rf_view_ui::hint("This mode has not been evaluated.");
        return;
    }
    if (mode->status == RfBudgetModeStatus::NotConfigured) {
        rf_view_ui::hint("The mode has no requirements. Set them in Receiver Requirements.");
        return;
    }
    if (mode->status == RfBudgetModeStatus::NotBudgeted) {
        rf_view_ui::hint("Not budgeted for this mode. Add a record in Mode overrides above.");
        return;
    }
    if (mode->cascades.empty()) {
        rf_view_ui::hint("No output is reached by this mode.");
    }

    for (const RfBudgetUnreachedOutput &unreached : mode->unreached)
        ImGui::TextColored(rf_view_ui::failColor(), "%s: %s", unreached.output.c_str(),
                           unreached.reason.c_str());

    if (mode->cascades.empty())
        return;
    m_cascade_output = std::min(m_cascade_output, mode->cascades.size() - 1);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("Output##cascade_output",
                          mode->cascades[m_cascade_output].output.c_str())) {
        for (std::size_t index = 0; index < mode->cascades.size(); ++index) {
            const bool selected = index == m_cascade_output;
            if (ImGui::Selectable(mode->cascades[index].output.c_str(), selected) && !selected) {
                m_cascade_output = index;
                m_cascade_point = 0;
            }
        }
        ImGui::EndCombo();
    }

    const RfBudgetCascade &cascade = mode->cascades[m_cascade_output];
    const std::size_t count = cascade.frequencies_Hz.size();
    if (count == 0 || cascade.stages.empty()) {
        rf_view_ui::hint("No cascade points.");
        return;
    }

    // The four requirement verdicts and the aggregate, side by side, so the outcome is readable
    // before the numbers.
    if (ImGui::BeginTable("budget_verdict", 5,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Overall");
        ImGui::TableSetupColumn("Gain");
        ImGui::TableSetupColumn("Noise figure");
        ImGui::TableSetupColumn("Output power");
        ImGui::TableSetupColumn("IIP3");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        rf_view_ui::statusBadge(cascade.verdict.overall);
        ImGui::TableNextColumn();
        rf_view_ui::metricStatus(cascade.verdict.gain);
        ImGui::TableNextColumn();
        rf_view_ui::metricStatus(cascade.verdict.noise_figure);
        ImGui::TableNextColumn();
        rf_view_ui::metricStatus(cascade.verdict.output_power);
        ImGui::TableNextColumn();
        rf_view_ui::metricStatus(cascade.verdict.iip3);
        ImGui::EndTable();
    }

    // --- Sweep ----------------------------------------------------------------------------------
    // The sweep comes before the band tables: it is what a user tunes against, so it stays above
    // the fold instead of being pushed off the bottom by two long tables.
    ImGui::SeparatorText("Sweep");
    const auto &session_config = requirements.modes[m_cascade_mode].config;
    std::optional<double> gain_min;
    std::optional<double> gain_max;
    std::optional<double> nf_max;
    if (session_config) {
        if (session_config->gain) {
            gain_min = session_config->gain->minimum_dB;
            gain_max = session_config->gain->maximum_dB;
        }
        nf_max = session_config->nf_max_dB;
    }
    const float plot_height = std::clamp(ImGui::GetContentRegionAvail().y * 0.4f, 140.0f, 240.0f);
    if (ImGui::GetContentRegionAvail().x >= 760.0f) {
        // Side by side when there is room: two stacked plots cost twice the vertical space.
        if (ImGui::BeginTable("budget_sweep_plots", 2,
                              ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawSeriesPlot("Total gain over the band", "Gain (dB)", cascade.frequencies_Hz,
                           cascade.stages.back().cumulative_gain_dB, gain_min, gain_max,
                           plot_height);
            ImGui::TableNextColumn();
            drawSeriesPlot("Total noise figure over the band", "NF (dB)", cascade.frequencies_Hz,
                           cascade.stages.back().cumulative_nf_dB, std::nullopt, nf_max,
                           plot_height);
            ImGui::EndTable();
        }
    } else {
        drawSeriesPlot("Total gain over the band", "Gain (dB)", cascade.frequencies_Hz,
                       cascade.stages.back().cumulative_gain_dB, gain_min, gain_max, plot_height);
        drawSeriesPlot("Total noise figure over the band", "NF (dB)", cascade.frequencies_Hz,
                       cascade.stages.back().cumulative_nf_dB, std::nullopt, nf_max, plot_height);
    }

    // --- Band detail ----------------------------------------------------------------------------
    // Collapsible: the verdict and the sweep above are the answer, these tables are the support.
    ImGui::PushID(cascade.output.c_str());
    if (ImGui::CollapsingHeader("Worst case over the band", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawBandSummary(cascade, evaluation);
    }
    if (ImGui::CollapsingHeader("At one frequency")) {
        drawPointDetail(cascade, evaluation);
    }
    ImGui::PopID();
}

void RfBudgetWidget::drawBandSummary(const RfBudgetCascade &cascade,
                                     const RfBudgetEvaluation &evaluation) const {
    if (ImGui::BeginTable("budget_band_summary", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Gain dB");
        ImGui::TableSetupColumn("NF dB");
        ImGui::TableSetupColumn("Cum gain dB");
        ImGui::TableSetupColumn("Cum NF dB");
        ImGui::TableSetupColumn("Level dBm");
        ImGui::TableSetupColumn("P1dB head dB");
        ImGui::TableHeadersRow();
        for (const RfBudgetStageResult &stage : cascade.stages) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s (%d/%d)", stage.stage.c_str(), stage.input_port, stage.output_port);
            ImGui::TableNextColumn();
            drawSeriesRangeCell(stage.gain_dB, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesRangeCell(stage.nf_dB, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesRangeCell(stage.cumulative_gain_dB, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesRangeCell(stage.cumulative_nf_dB, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesRangeCell(stage.level_dBm, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesRangeCell(stage.p1db_headroom_dB, evaluation.reasons);
        }
        ImGui::EndTable();
    }
}

void RfBudgetWidget::drawPointDetail(const RfBudgetCascade &cascade,
                                     const RfBudgetEvaluation &evaluation) {
    const std::size_t count = cascade.frequencies_Hz.size();
    m_cascade_point = std::clamp(m_cascade_point, 0, static_cast<int>(count) - 1);
    ImGui::SetNextItemWidth(260.0f);
    ImGui::SliderInt("Point", &m_cascade_point, 0, static_cast<int>(count) - 1);
    ImGui::SameLine();
    ImGui::Text("%.6g Hz", cascade.frequencies_Hz[static_cast<std::size_t>(m_cascade_point)]);

    const std::size_t point = static_cast<std::size_t>(m_cascade_point);
    if (ImGui::BeginTable("budget_cascade", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Gain dB");
        ImGui::TableSetupColumn("NF dB");
        ImGui::TableSetupColumn("OIP3 dBm");
        ImGui::TableSetupColumn("Cum gain dB");
        ImGui::TableSetupColumn("Cum NF dB");
        ImGui::TableSetupColumn("Cum IIP3 dBm");
        ImGui::TableSetupColumn("Level dBm");
        ImGui::TableSetupColumn("NF share %");
        ImGui::TableSetupColumn("P1dB head dB");
        ImGui::TableHeadersRow();
        for (const RfBudgetStageResult &stage : cascade.stages) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s (%d/%d)", stage.stage.c_str(), stage.input_port, stage.output_port);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.gain_dB, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.nf_dB, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.oip3_dBm, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.cumulative_gain_dB, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.cumulative_nf_dB, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.cumulative_iip3_dBm, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.level_dBm, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.nf_share_percent, point, evaluation.reasons);
            ImGui::TableNextColumn();
            drawSeriesCell(stage.p1db_headroom_dB, point, evaluation.reasons);
        }
        ImGui::EndTable();
    }
}

void RfBudgetWidget::drawSeriesPlot(const char *label, const char *y_label,
                                    const std::vector<double> &frequencies,
                                    const RfBudgetSeries &series, std::optional<double> low_limit,
                                    std::optional<double> high_limit, float height) const {
    const std::size_t count = std::min(frequencies.size(), series.values.size());
    std::vector<double> values(count, std::numeric_limits<double>::quiet_NaN());
    bool any_finite = false;
    for (std::size_t index = 0; index < count; ++index) {
        // NaN and infinities break the line: a stage that passes no signal has no curve there.
        if (std::isfinite(series.values[index])) {
            values[index] = series.values[index];
            any_finite = true;
        }
    }
    if (count < 2 || !any_finite) {
        ImGui::TextDisabled("%s: no finite sweep values.", label);
        return;
    }
    if (!ImPlot::BeginPlot(label, ImVec2(-1.0f, height)))
        return;
    ImPlot::SetupAxes("Frequency (Hz)", y_label, ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
    ImPlotSpec limit_spec;
    limit_spec.Flags = ImPlotInfLinesFlags_Horizontal;
    limit_spec.LineColor = rf_view_ui::warnColor();
    if (low_limit) {
        const double limit = *low_limit;
        ImPlot::PlotInfLines("Requirement min", &limit, 1, limit_spec);
    }
    if (high_limit) {
        const double limit = *high_limit;
        ImPlot::PlotInfLines("Requirement max", &limit, 1, limit_spec);
    }
    ImPlot::PlotLine("Cumulative", frequencies.data(), values.data(), static_cast<int>(count));
    ImPlot::EndPlot();
}

void RfBudgetWidget::drawModals(RfBudget &budget) {
    if (m_modal == Modal::RenameStage) {
        if (!ImGui::IsPopupOpen(kRenameStageTitle))
            ImGui::OpenPopup(kRenameStageTitle);
        if (ImGui::BeginPopupModal(kRenameStageTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_name_buffer.data(), m_name_buffer.size());
            if (!m_error.empty())
                ImGui::TextColored(rf_view_ui::failColor(), "%s", m_error.c_str());
            if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
                std::string error;
                if (renameRfBudgetStage(budget, m_modal_stage, m_name_buffer.data(), error)) {
                    m_modal = Modal::None;
                    m_error.clear();
                    markChanged();
                    ImGui::CloseCurrentPopup();
                } else {
                    m_error = std::move(error);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                m_modal = Modal::None;
                m_error.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else if (m_modal == Modal::RenameOutput) {
        if (!ImGui::IsPopupOpen(kRenameOutputTitle))
            ImGui::OpenPopup(kRenameOutputTitle);
        if (ImGui::BeginPopupModal(kRenameOutputTitle, nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_name_buffer.data(), m_name_buffer.size());
            if (!m_error.empty())
                ImGui::TextColored(rf_view_ui::failColor(), "%s", m_error.c_str());
            if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
                std::string error;
                if (renameRfBudgetOutput(budget, m_modal_output, m_name_buffer.data(), error)) {
                    m_modal = Modal::None;
                    m_error.clear();
                    markChanged();
                    ImGui::CloseCurrentPopup();
                } else {
                    m_error = std::move(error);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                m_modal = Modal::None;
                m_error.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else if (m_modal == Modal::StageFeeds) {
        if (!ImGui::IsPopupOpen(kFeedsTitle))
            ImGui::OpenPopup(kFeedsTitle);
        if (ImGui::BeginPopupModal(kFeedsTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_modal_stage >= budget.stages.size()) {
                m_modal = Modal::None;
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::TextUnformatted(budget.stages[m_modal_stage].name.c_str());
                ImGui::TextDisabled("What drives each input port");
                const auto options = feedOptions(budget, m_modal_stage);
                for (std::size_t port = 0; port < budget.stages[m_modal_stage].inputs.size();
                     ++port) {
                    const int current = feedOptionIndex(budget, m_modal_stage,
                                                        budget.stages[m_modal_stage].inputs[port]);
                    ImGui::PushID(static_cast<int>(port));
                    ImGui::TextDisabled("in %zu", port);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::BeginCombo("##feed",
                                          options[static_cast<std::size_t>(current)].c_str())) {
                        for (std::size_t option = 0; option < options.size(); ++option) {
                            const bool selected = static_cast<int>(option) == current;
                            if (ImGui::Selectable(options[option].c_str(), selected) && !selected) {
                                budget.stages[m_modal_stage].inputs[port] =
                                    feedFromOption(static_cast<int>(option), budget, m_modal_stage);
                                markChanged();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) {
                    m_modal = Modal::None;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    } else if (m_modal == Modal::StageParams) {
        if (!ImGui::IsPopupOpen(kParamsTitle))
            ImGui::OpenPopup(kParamsTitle);
        if (ImGui::BeginPopupModal(kParamsTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_modal_stage >= budget.stages.size()) {
                m_modal = Modal::None;
                ImGui::CloseCurrentPopup();
            } else {
                RfBudgetStage &stage = budget.stages[m_modal_stage];
                ImGui::Text("%s (%s)", stage.name.c_str(), stageTypeLabel(stage.type).c_str());
                if (stage.source == RfBudgetStageSource::Library) {
                    ImGui::TextDisabled("A library stage takes no params; edit the part instead.");
                } else {
                    if (!stage.params.is_object())
                        stage.params = nlohmann::ordered_json::object();
                    for (const ParameterField *field : editableFields(stage.type)) {
                        ImGui::PushID(field->key.c_str());
                        const bool present = paramsHas(stage, field->key);
                        const std::string label = field->unit.empty()
                                                      ? field->label
                                                      : field->label + " (" + field->unit + ")";
                        if (field->kind == FieldKind::Bool) {
                            if (!present) {
                                ImGui::TextDisabled("%s: default", label.c_str());
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Override")) {
                                    stage.params[field->key] = false;
                                    markChanged();
                                }
                            } else {
                                const nlohmann::ordered_json &stored = stage.params[field->key];
                                bool value = stored.is_boolean() ? stored.get<bool>()
                                                                 : (stored.is_number() &&
                                                                    stored.get<double>() != 0.0);
                                if (ImGui::Checkbox(label.c_str(), &value)) {
                                    stage.params[field->key] = value;
                                    markChanged();
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Clear")) {
                                    stage.params.erase(field->key);
                                    markChanged();
                                }
                            }
                        } else if (field->kind == FieldKind::Enum) {
                            if (!present) {
                                ImGui::TextDisabled("%s: default", label.c_str());
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Override")) {
                                    stage.params[field->key] = 0;
                                    markChanged();
                                }
                            } else {
                                int current =
                                    stage.params[field->key].is_number()
                                        ? static_cast<int>(stage.params[field->key].get<double>())
                                        : 0;
                                const char *preview =
                                    current >= 0 && static_cast<std::size_t>(current) <
                                                        field->enum_values.size()
                                        ? field->enum_values[static_cast<std::size_t>(current)]
                                              .c_str()
                                        : "(index)";
                                ImGui::SetNextItemWidth(160.0f);
                                if (ImGui::BeginCombo(label.c_str(), preview)) {
                                    for (std::size_t option = 0; option < field->enum_values.size();
                                         ++option) {
                                        const bool selected = static_cast<int>(option) == current;
                                        if (ImGui::Selectable(field->enum_values[option].c_str(),
                                                              selected) &&
                                            !selected) {
                                            stage.params[field->key] = static_cast<int>(option);
                                            markChanged();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Clear")) {
                                    stage.params.erase(field->key);
                                    markChanged();
                                }
                            }
                        } else if (field->kind == FieldKind::Number) {
                            if (!present) {
                                ImGui::TextDisabled("%s: default", label.c_str());
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Override")) {
                                    stage.params[field->key] =
                                        std::clamp(0.0, field->min, field->max);
                                    markChanged();
                                }
                            } else if (stage.params[field->key].is_number()) {
                                double value = stage.params[field->key].get<double>();
                                if (utils::inputDouble(label, value, 0.1, 1.0, "%.6g", field->min,
                                                       field->max)) {
                                    stage.params[field->key] = value;
                                    markChanged();
                                }
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Clear")) {
                                    stage.params.erase(field->key);
                                    markChanged();
                                }
                            }
                        }
                        ImGui::PopID();
                    }
                    std::vector<std::string> unknown;
                    for (auto it = stage.params.begin(); it != stage.params.end(); ++it) {
                        if (!findField(stage.type, it.key()))
                            unknown.push_back(it.key());
                    }
                    for (const std::string &key : unknown) {
                        ImGui::TextColored(rf_view_ui::failColor(), "Unknown setting '%s'",
                                           key.c_str());
                        ImGui::SameLine();
                        ImGui::PushID(key.c_str());
                        if (ImGui::SmallButton("Remove")) {
                            stage.params.erase(key);
                            markChanged();
                        }
                        ImGui::PopID();
                    }
                }
                if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) {
                    m_modal = Modal::None;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }
}
