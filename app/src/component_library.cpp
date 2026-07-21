#include "component_library.h"
#include "logging_core.h"
#include <filesystem>
#include <fstream>

void ComponentLibrary::loadFile(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        LOG_WARN("ComponentLibrary: cannot open file: %s", filepath.c_str());
        return;
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const nlohmann::json::parse_error& e) {
        LOG_WARN("ComponentLibrary: JSON parse error in %s: %s", filepath.c_str(), e.what());
        return;
    }

    if (!j.contains("type") || !j.contains("part_number") || !j.contains("parameters")) {
        LOG_WARN("ComponentLibrary: missing required fields in %s", filepath.c_str());
        return;
    }

    ComponentDefinition def;
    def.schema_version = j.value("schema_version", 1);
    def.type = j["type"].get<std::string>();
    def.part_number = j["part_number"].get<std::string>();
    def.manufacturer = j.value("manufacturer", "");
    def.description = j.value("description", "");
    def.parameters = j["parameters"];
    def.test_conditions = j.value("test_conditions", nlohmann::json::object());
    def.notes = j.value("notes", "");
    def.source_path = filepath;

    m_definitions.push_back(std::move(def));
}

std::vector<const ComponentDefinition*> ComponentLibrary::all() const {
    std::vector<const ComponentDefinition*> result;
    result.reserve(m_definitions.size());
    for (const auto& def : m_definitions) {
        result.push_back(&def);
    }
    return result;
}

void ComponentLibrary::scan(const std::string& directory) {
    namespace fs = std::filesystem;
    if (!fs::exists(directory)) return;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            loadFile(entry.path().string());
        }
    }
}

std::vector<const ComponentDefinition*> ComponentLibrary::byType(const std::string& type) const {
    std::vector<const ComponentDefinition*> result;
    for (const auto& def : m_definitions) {
        if (def.type == type) result.push_back(&def);
    }
    return result;
}
