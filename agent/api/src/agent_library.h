#pragma once

#include "component_library.h"

#include <nlohmann/json.hpp>

#include <string_view>
#include <vector>

// Library-part lookup shared by circuit_edit and data_file_read.
// matchingParts returns exact part-number matches sorted by (part_number, type, manufacturer).
// partCandidates returns up to five substring matches in the same order.
std::vector<ComponentDefinition> matchingParts(const ComponentLibrary &library,
                                               std::string_view part_number, std::string_view type,
                                               bool type_supplied);
nlohmann::json partCandidates(const ComponentLibrary &library, std::string_view part_number);
