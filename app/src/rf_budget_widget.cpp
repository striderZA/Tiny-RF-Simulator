#include "rf_budget_widget.h"

#include "component_library.h"
#include "component_type_registry.h"
#include "imgui.h"
#include "receiver_requirements_widget.h"
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

constexpr ImVec4 kErrorColor(1.0f, 0.35f, 0.35f, 1.0f);
constexpr ImVec4 kPassColor(0.25f, 0.85f, 0.35f, 1.0f);
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

std::string feedText(const std::optional<RfBudgetFeed> &feed) {
    if (!feed)
        return "unfed";
    if (!feed->stage)
        return "budget input";
    return *feed->stage + " out " + std::to_string(feed->port);
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

void textColoredByStatus(ReceiverRequirementStatus status, const char *text) {
    switch (receiverRequirementStatusTone(status)) {
    case ReceiverRequirementStatusTone::PassGreen:
        ImGui::TextColored(kPassColor, "%s", text);
        break;
    case ReceiverRequirementStatusTone::FailRed:
        ImGui::TextColored(kErrorColor, "%s", text);
        break;
    case ReceiverRequirementStatusTone::Neutral:
        ImGui::TextDisabled("%s", text);
        break;
    }
}

// One cascade cell. A NaN point is blank and carries a reason; an infinity is a real value
// (no transmission, linear OIP3, infinite headroom) and carries its reason too.
void drawSeriesCell(const RfBudgetSeries &series, std::size_t point,
                    const RfBudgetReasonTable &reasons) {
    if (point >= series.values.size()) {
        ImGui::TextDisabled("—");
        return;
    }
    const double value = series.values[point];
    const std::uint16_t reason_index = point < series.reasons.size() ? series.reasons[point] : 0;
    const std::string &reason = reasons.at(reason_index);
    if (std::isnan(value))
        ImGui::TextDisabled("—");
    else if (std::isinf(value))
        ImGui::Text("%s", value > 0.0 ? "+inf" : "-inf");
    else
        ImGui::Text("%.2f", value);
    if (!reason.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", reason.c_str());
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
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Points", &points)) {
        points = std::clamp(points, kMinRfBudgetPoints, kMaxRfBudgetPoints);
        if (points != budget.point_count) {
            budget.point_count = points;
            markChanged();
        }
    }
    if (!m_error.empty())
        ImGui::TextColored(kErrorColor, "%s", m_error.c_str());
}

void RfBudgetWidget::drawStatus(const RfBudget &budget,
                                const RfBudgetEvaluation &evaluation) const {
    if (auto reason = validateRfBudget(budget))
        ImGui::TextColored(kErrorColor, "%s", reason->c_str());
    else if (budget.empty())
        ImGui::TextDisabled("No stages yet. Add a stage to describe the chain.");
    else if (!evaluation.not_evaluated_reason.empty())
        ImGui::TextDisabled("%s", evaluation.not_evaluated_reason.c_str());
    else
        ImGui::TextDisabled("Evaluated %zu mode(s).", evaluation.modes.size());
}

void RfBudgetWidget::drawStages(RfBudget &budget, const ComponentLibrary &library) {
    ImGui::SeparatorText("Stages");
    if (!ImGui::BeginTable("budget_stages", 6,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("Part / Params");
    ImGui::TableSetupColumn("Feeds");
    ImGui::TableSetupColumn("##actions");
    ImGui::TableHeadersRow();

    for (std::size_t index = 0; index < budget.stages.size(); ++index) {
        RfBudgetStage &stage = budget.stages[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(stage.name.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::BeginCombo("##type", stage.type.c_str())) {
            for (const RfBudgetStageType &type : rfBudgetStageTypes()) {
                const bool selected = type.type == stage.type;
                if (ImGui::Selectable(std::string(type.type).c_str(), selected) && !selected) {
                    setRfBudgetStageType(budget, index, type.type);
                    markChanged();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(110.0f);
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
            ImGui::SetNextItemWidth(160.0f);
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
                ImGui::TextDisabled("No %s parts in the library.", stage.type.c_str());
        } else if (ImGui::Button("Params...")) {
            m_modal = Modal::StageParams;
            m_modal_stage = index;
            m_error.clear();
        }

        ImGui::TableNextColumn();
        std::string feeds;
        for (std::size_t port = 0; port < stage.inputs.size(); ++port) {
            if (port > 0)
                feeds += "\n";
            feeds += "in " + std::to_string(port) + ": " + feedText(stage.inputs[port]);
        }
        if (feeds.empty())
            feeds = "no inputs";
        ImGui::TextUnformatted(feeds.c_str());
        ImGui::SameLine();
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
    ImGui::SeparatorText("Outputs");
    if (budget.outputs.empty()) {
        ImGui::TextDisabled("No outputs. Add one to evaluate a cascade.");
        return;
    }
    if (!ImGui::BeginTable("budget_outputs", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Stage");
    ImGui::TableSetupColumn("Port");
    ImGui::TableSetupColumn("##actions");
    ImGui::TableHeadersRow();

    for (std::size_t index = 0; index < budget.outputs.size(); ++index) {
        RfBudgetOutput &output = budget.outputs[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(output.name.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(140.0f);
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
        ImGui::SetNextItemWidth(80.0f);
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
    ImGui::SeparatorText("Mode overrides");
    if (requirements.modes.empty()) {
        ImGui::TextDisabled("No requirement modes. Add a mode first.");
        return;
    }
    m_override_mode = std::min(m_override_mode, requirements.modes.size() - 1);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Mode##override_mode",
                          requirements.modes[m_override_mode].name.c_str())) {
        for (std::size_t index = 0; index < requirements.modes.size(); ++index) {
            const bool selected = index == m_override_mode;
            if (ImGui::Selectable(requirements.modes[index].name.c_str(), selected) && !selected) {
                m_override_mode = index;
                m_override_stage = 0;
                m_override_key = 0;
            }
        }
        ImGui::EndCombo();
    }

    const std::uint32_t mode_id = requirements.modes[m_override_mode].id;
    RfBudgetModeRecord *record = nullptr;
    for (RfBudgetModeRecord &candidate : budget.modes) {
        if (candidate.mode_id == mode_id) {
            record = &candidate;
            break;
        }
    }
    if (!record) {
        ImGui::TextDisabled("Not budgeted for this mode.");
    } else if (ImGui::BeginTable("budget_overrides", 4,
                                 ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Setting");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("##actions");
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
                    ImGui::SetNextItemWidth(100.0f);
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
        ImGui::TextDisabled("Add a stage before overriding a setting.");
        return;
    }
    m_override_stage = std::min(m_override_stage, budget.stages.size() - 1);
    ImGui::SetNextItemWidth(160.0f);
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
    ImGui::SetNextItemWidth(160.0f);
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
    ImGui::BeginDisabled(fields.empty());
    if (ImGui::Button("Add override")) {
        const ParameterField &field = *fields[m_override_key];
        double value = 0.0;
        if (paramsHas(budget.stages[m_override_stage], field.key)) {
            const auto &stored = budget.stages[m_override_stage].params[field.key];
            if (stored.is_number())
                value = stored.get<double>();
        }
        setRfBudgetOverride(budget, mode_id, budget.stages[m_override_stage].name, field.key,
                            value);
        markChanged();
    }
    ImGui::EndDisabled();
}

void RfBudgetWidget::drawCascade(const RfBudget &budget,
                                 const ReceiverRequirementsSet &requirements,
                                 const RfBudgetEvaluation &evaluation) {
    ImGui::SeparatorText("Cascade");
    if (requirements.modes.empty()) {
        ImGui::TextDisabled("No requirement modes.");
        return;
    }
    m_cascade_mode = std::min(m_cascade_mode, requirements.modes.size() - 1);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Mode##cascade_mode", requirements.modes[m_cascade_mode].name.c_str())) {
        for (std::size_t index = 0; index < requirements.modes.size(); ++index) {
            const bool selected = index == m_cascade_mode;
            if (ImGui::Selectable(requirements.modes[index].name.c_str(), selected) && !selected) {
                m_cascade_mode = index;
                m_cascade_output = 0;
                m_cascade_point = 0;
            }
        }
        ImGui::EndCombo();
    }

    const RfBudgetModeResult *mode =
        findRfBudgetModeResult(evaluation, requirements.modes[m_cascade_mode].id);
    if (!mode) {
        ImGui::TextDisabled("This mode has not been evaluated.");
        return;
    }
    if (mode->status == RfBudgetModeStatus::NotConfigured) {
        ImGui::TextDisabled("The mode has no requirements.");
        return;
    }
    if (mode->status == RfBudgetModeStatus::NotBudgeted) {
        ImGui::TextDisabled("Not budgeted for this mode.");
        return;
    }
    if (mode->cascades.empty())
        ImGui::TextDisabled("No output is reached by this mode.");

    for (const RfBudgetUnreachedOutput &unreached : mode->unreached)
        ImGui::TextColored(kErrorColor, "%s: %s", unreached.output.c_str(),
                           unreached.reason.c_str());

    if (mode->cascades.empty())
        return;
    m_cascade_output = std::min(m_cascade_output, mode->cascades.size() - 1);
    ImGui::SetNextItemWidth(180.0f);
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
        ImGui::TextDisabled("No cascade points.");
        return;
    }
    m_cascade_point = std::clamp(m_cascade_point, 0, static_cast<int>(count) - 1);
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderInt("Point", &m_cascade_point, 0, static_cast<int>(count) - 1);
    ImGui::SameLine();
    ImGui::Text("%.6g Hz", cascade.frequencies_Hz[static_cast<std::size_t>(m_cascade_point)]);

    const std::string verdict = std::string("Verdict: ") + statusText(cascade.verdict.overall);
    textColoredByStatus(cascade.verdict.overall, verdict.c_str());

    const std::size_t point = static_cast<std::size_t>(m_cascade_point);
    if (ImGui::BeginTable("budget_cascade", 10,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX)) {
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
            ImGui::Text("%s (%d→%d)", stage.stage.c_str(), stage.input_port, stage.output_port);
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

    drawSeriesPlot("Cumulative gain (dB)", cascade.stages.back().cumulative_gain_dB, 110.0f);
    drawSeriesPlot("Cumulative noise figure (dB)", cascade.stages.back().cumulative_nf_dB, 110.0f);
}

void RfBudgetWidget::drawSeriesPlot(const char *label, const RfBudgetSeries &series, float height) {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (double value : series.values) {
        if (!std::isfinite(value))
            continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    ImGui::TextUnformatted(label);
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        ImGui::TextDisabled("No values.");
        return;
    }
    if (maximum == minimum) {
        minimum -= 1.0;
        maximum += 1.0;
    }
    ImGui::TextDisabled("%+.2f to %+.2f", minimum, maximum);
    const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                  ImGui::GetColorU32(ImGuiCol_Border));
    for (int line = 1; line < 4; ++line) {
        const float y = origin.y + size.y * static_cast<float>(line) / 4.0f;
        draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
                      ImGui::GetColorU32(ImGuiCol_Border, 0.35f));
    }
    const std::size_t count = series.values.size();
    if (count < 2)
        return;
    const auto point = [&](std::size_t index, double value) {
        const float x =
            origin.x + size.x * static_cast<float>(index) / static_cast<float>(count - 1);
        const float y =
            origin.y + size.y * static_cast<float>((maximum - value) / (maximum - minimum));
        return ImVec2(x, y);
    };
    for (std::size_t index = 1; index < count; ++index) {
        const double previous = series.values[index - 1];
        const double current = series.values[index];
        if (!std::isfinite(previous) || !std::isfinite(current))
            continue;
        draw->AddLine(point(index - 1, previous), point(index, current),
                      ImGui::GetColorU32(ImGuiCol_PlotLines), 1.5f);
    }
}

void RfBudgetWidget::drawModals(RfBudget &budget) {
    if (m_modal == Modal::RenameStage) {
        if (!ImGui::IsPopupOpen(kRenameStageTitle))
            ImGui::OpenPopup(kRenameStageTitle);
        if (ImGui::BeginPopupModal(kRenameStageTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_name_buffer.data(), m_name_buffer.size());
            if (!m_error.empty())
                ImGui::TextColored(kErrorColor, "%s", m_error.c_str());
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
                ImGui::TextColored(kErrorColor, "%s", m_error.c_str());
            if (ImGui::Button("OK", ImVec2(100.0f, 0.0f))) {
                if (m_modal_output < budget.outputs.size()) {
                    budget.outputs[m_modal_output].name = m_name_buffer.data();
                    m_modal = Modal::None;
                    m_error.clear();
                    markChanged();
                    ImGui::CloseCurrentPopup();
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
                const auto options = feedOptions(budget, m_modal_stage);
                for (std::size_t port = 0; port < budget.stages[m_modal_stage].inputs.size();
                     ++port) {
                    const int current = feedOptionIndex(budget, m_modal_stage,
                                                        budget.stages[m_modal_stage].inputs[port]);
                    ImGui::PushID(static_cast<int>(port));
                    ImGui::SetNextItemWidth(240.0f);
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
                ImGui::Text("%s (%s)", stage.name.c_str(), stage.type.c_str());
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
                        ImGui::TextColored(kErrorColor, "Unknown setting '%s'", key.c_str());
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
