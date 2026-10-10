#include "agent_library.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <tuple>
#include <vector>

namespace {

using Json = nlohmann::json;

std::string lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

} // namespace

std::vector<ComponentDefinition> matchingParts(const ComponentLibrary &library,
                                               std::string_view part_number, std::string_view type,
                                               bool type_supplied) {
    std::vector<ComponentDefinition> matches;
    const std::string wanted = lower(part_number);
    for (const ComponentDefinition *definition : library.all()) {
        if (lower(definition->part_number) != wanted)
            continue;
        if (type_supplied && definition->type != type)
            continue;
        matches.push_back(*definition);
    }
    std::sort(matches.begin(), matches.end(),
              [](const ComponentDefinition &left, const ComponentDefinition &right) {
                  return std::tie(left.part_number, left.type, left.manufacturer) <
                         std::tie(right.part_number, right.type, right.manufacturer);
              });
    return matches;
}

Json partCandidates(const ComponentLibrary &library, std::string_view part_number) {
    std::vector<const ComponentDefinition *> candidates;
    const std::string wanted = lower(part_number);
    for (const ComponentDefinition *definition : library.all()) {
        const std::string candidate = lower(definition->part_number);
        if (candidate.find(wanted) == std::string::npos)
            continue;
        candidates.push_back(definition);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto *left, const auto *right) {
        return std::tie(left->part_number, left->type, left->manufacturer) <
               std::tie(right->part_number, right->type, right->manufacturer);
    });
    if (candidates.size() > 5)
        candidates.resize(5);
    Json result = Json::array();
    for (const ComponentDefinition *candidate : candidates) {
        result.push_back({{"part_number", candidate->part_number},
                          {"type", candidate->type},
                          {"manufacturer", candidate->manufacturer}});
    }
    return result;
}
