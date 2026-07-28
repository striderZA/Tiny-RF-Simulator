// app/src/component_form_model.cpp
#include <filesystem>
#include "component_form_model.h"

ComponentFormModel::ComponentFormModel(const ComponentTypeDescriptor &descriptor)
    : m_descriptor(descriptor) {
    for (const auto &field : descriptor.fields) {
        if (!field.default_value.is_null())
            m_parameters[field.key] = field.default_value;
    }
}

void ComponentFormModel::loadFrom(const ComponentDefinition &def) {
    m_part_number = def.part_number;
    m_manufacturer = def.manufacturer;
    m_description = def.description;
    m_notes = def.notes;
    m_source_path = def.source_path;
    m_original_data_files = def.data_files;
    m_parameters = def.parameters;
}

void ComponentFormModel::setParameter(const std::string &key, const nlohmann::json &value) {
    m_parameters[key] = value;
}

nlohmann::json ComponentFormModel::parameter(const std::string &key) const {
    if (m_parameters.contains(key))
        return m_parameters[key];
    return nlohmann::json();
}

std::vector<ValidationIssue> ComponentFormModel::validate(const ComponentLibrary &library) const {
    auto issues = library.validate(m_descriptor.type, m_parameters);
    if (m_part_number.empty())
        issues.push_back({"part_number", "Part number is required"});
    return issues;
}

ComponentDefinition ComponentFormModel::buildDefinition() const {
    ComponentDefinition def;
    def.schema_version = 2;
    def.type = m_descriptor.type;
    def.part_number = m_part_number;
    def.manufacturer = m_manufacturer;
    def.description = m_description;
    def.notes = m_notes;
    def.parameters = m_parameters;
    def.source_path = m_source_path;
    if (!m_sparam_source_path.empty()) {
        auto ext = std::filesystem::path(m_sparam_source_path).extension().string();
        def.data_files.push_back({"s_parameters", m_part_number + ext});
    } else if (!m_original_data_files.empty()) {
        def.data_files = m_original_data_files;
    }
    return def;
}
