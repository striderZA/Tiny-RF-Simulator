// app/include/component_form_model.h
#pragma once

#include "component_library.h"
#include "component_type_registry.h"
#include <nlohmann/json.hpp>
#include <string>

class ComponentFormModel {
  public:
    explicit ComponentFormModel(const ComponentTypeDescriptor &descriptor);

    void loadFrom(const ComponentDefinition &def);

    const ComponentTypeDescriptor &descriptor() const { return m_descriptor; }

    void setPartNumber(const std::string &v) { m_part_number = v; }
    const std::string &partNumber() const { return m_part_number; }
    void setManufacturer(const std::string &v) { m_manufacturer = v; }
    const std::string &manufacturer() const { return m_manufacturer; }
    void setDescription(const std::string &v) { m_description = v; }
    const std::string &description() const { return m_description; }
    void setNotes(const std::string &v) { m_notes = v; }
    const std::string &notes() const { return m_notes; }

    void setParameter(const std::string &key, const nlohmann::json &value);
    nlohmann::json parameter(const std::string &key) const;

    // Staged absolute path to a picked S-param file; copy-into-place happens
    // at save time (Task 6), not here.
    void setSparamSourcePath(const std::string &path) { m_sparam_source_path = path; }
    const std::string &sparamSourcePath() const { return m_sparam_source_path; }

    // Original source_path when editing an existing entry; empty for a new entry.
    void setSourcePath(const std::string &path) { m_source_path = path; }
    const std::string &sourcePath() const { return m_source_path; }

    std::vector<ValidationIssue> validate(const ComponentLibrary &library) const;
    ComponentDefinition buildDefinition() const;

  private:
    const ComponentTypeDescriptor &m_descriptor;
    std::string m_part_number;
    std::string m_manufacturer;
    std::string m_description;
    std::string m_notes;
    std::string m_sparam_source_path;
    std::string m_source_path;
    nlohmann::json m_parameters = nlohmann::json::object();
    std::vector<DataFileRef> m_original_data_files;
};
