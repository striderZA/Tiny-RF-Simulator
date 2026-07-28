// app/src/component_form_widget.cpp
#include "component_form_widget.h"
#include "component_library.h"
#include <cstring>
#include <imgui.h>
#include <portable-file-dialogs.h>

ComponentFormWidget::ComponentFormWidget(ComponentFormModel &model) : m_model(&model) {}

void ComponentFormWidget::initBuffersOnce() {
    if (m_buffers_initialized)
        return;
    std::strncpy(m_part_number_buf, m_model->partNumber().c_str(), sizeof(m_part_number_buf) - 1);
    std::strncpy(m_manufacturer_buf, m_model->manufacturer().c_str(),
                 sizeof(m_manufacturer_buf) - 1);
    std::strncpy(m_description_buf, m_model->description().c_str(), sizeof(m_description_buf) - 1);
    std::strncpy(m_notes_buf, m_model->notes().c_str(), sizeof(m_notes_buf) - 1);
    m_buffers_initialized = true;
}

bool ComponentFormWidget::draw(const ComponentLibrary &library) {
    initBuffersOnce();
    auto issues = m_model->validate(library);
    auto issue_for = [&](const std::string &key) -> const ValidationIssue * {
        for (auto &i : issues)
            if (i.field == key)
                return &i;
        return nullptr;
    };

    if (ImGui::InputText("Part Number", m_part_number_buf, sizeof(m_part_number_buf)))
        m_model->setPartNumber(m_part_number_buf);
    if (const auto *issue = issue_for("part_number"))
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", issue->message.c_str());
    if (ImGui::InputText("Manufacturer", m_manufacturer_buf, sizeof(m_manufacturer_buf)))
        m_model->setManufacturer(m_manufacturer_buf);
    if (ImGui::InputText("Description", m_description_buf, sizeof(m_description_buf)))
        m_model->setDescription(m_description_buf);
    if (ImGui::InputTextMultiline("Notes", m_notes_buf, sizeof(m_notes_buf)))
        m_model->setNotes(m_notes_buf);

    ImGui::Separator();


    for (const auto &field : m_model->descriptor().fields) {
        ImGui::PushID(field.key.c_str());
        std::string label = field.label;
        if (!field.unit.empty())
            label += " (" + field.unit + ")";

        switch (field.kind) {
        case FieldKind::Number: {
            double v = m_model->parameter(field.key).is_number()
                           ? m_model->parameter(field.key).get<double>()
                           : 0.0;
            if (ImGui::InputDouble(label.c_str(), &v))
                m_model->setParameter(field.key, v);
            break;
        }
        case FieldKind::Bool: {
            bool v = m_model->parameter(field.key).is_boolean()
                         ? m_model->parameter(field.key).get<bool>()
                         : false;
            if (ImGui::Checkbox(label.c_str(), &v))
                m_model->setParameter(field.key, v);
            break;
        }
        case FieldKind::String: {
            char buf[256] = {};
            auto current = m_model->parameter(field.key);
            if (current.is_string())
                std::strncpy(buf, current.get<std::string>().c_str(), sizeof(buf) - 1);
            if (ImGui::InputText(label.c_str(), buf, sizeof(buf)))
                m_model->setParameter(field.key, std::string(buf));
            break;
        }
        case FieldKind::Enum: {
            auto current = m_model->parameter(field.key);
            std::string current_s = current.is_string() ? current.get<std::string>() : "";
            if (ImGui::BeginCombo(label.c_str(), current_s.c_str())) {
                for (const auto &opt : field.enum_values) {
                    bool selected = (opt == current_s);
                    if (ImGui::Selectable(opt.c_str(), selected))
                        m_model->setParameter(field.key, opt);
                }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldKind::FilePath: {
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_model->sparamSourcePath().empty()
                                          ? "(none)"
                                          : m_model->sparamSourcePath().c_str());
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                auto result = pfd::open_file("Select S-parameter file", "",
                                             {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"})
                                  .result();
                if (!result.empty())
                    m_model->setSparamSourcePath(result[0]);
            }
            break;
        }
        }

        if (!field.help.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", field.help.c_str());

        if (const auto *issue = issue_for(field.key)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", issue->message.c_str());
        }
        ImGui::PopID();
    }

    ImGui::Separator();

    bool valid = issues.empty();
    ImGui::BeginDisabled(!valid);
    bool save_clicked = ImGui::Button("Save");
    ImGui::EndDisabled();
    if (!valid) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu issue%s)", issues.size(), issues.size() == 1 ? "" : "s");
    }
    // Whole-definition issues (no specific field) — surface above the Save button
    for (const auto &issue : issues) {
        if (issue.field.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", issue.message.c_str());
    }
    return save_clicked;
}
