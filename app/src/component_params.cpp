#include "component_params.h"
#include "component_interface.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace {
using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

struct RequestedScalar {
    std::string path;
    Json input;
    Json value;
    bool floating = false;
};

ParamWriteResult failure(ParamWriteStatus status, std::string path = {},
                         std::string expected = {}) {
    ParamWriteResult result;
    result.status = status;
    result.path = std::move(path);
    result.expected = std::move(expected);
    return result;
}

bool isPathParameter(std::string_view key) {
    return key == "sparam_filepath" || key == "sparam_path";
}

bool isInteger(const Json &value) {
    return value.is_number_integer() || value.is_number_unsigned();
}

bool equalJson(const Json &lhs, const Json &rhs) {
    if (lhs.is_number() && rhs.is_number()) {
        if (lhs.is_number_float() || rhs.is_number_float())
            return lhs.is_number_float() && rhs.is_number_float() &&
                   lhs.get<double>() == rhs.get<double>();
        return isInteger(lhs) && isInteger(rhs) && lhs == rhs;
    }
    if (lhs.type() != rhs.type())
        return false;
    if (lhs.is_object()) {
        if (lhs.size() != rhs.size())
            return false;
        for (auto it = lhs.begin(); it != lhs.end(); ++it) {
            const auto other = rhs.find(it.key());
            if (other == rhs.end() || !equalJson(it.value(), *other))
                return false;
        }
        return true;
    }
    if (lhs.is_array()) {
        if (lhs.size() != rhs.size())
            return false;
        for (std::size_t index = 0; index < lhs.size(); ++index)
            if (!equalJson(lhs[index], rhs[index]))
                return false;
        return true;
    }
    return lhs == rhs;
}

const Json *valueAtPath(const Json &root, std::string_view path) {
    const Json *value = &root;
    std::size_t position = 0;
    while (position < path.size()) {
        if (path[position] == '.') {
            ++position;
            continue;
        }
        if (path[position] == '[') {
            const std::size_t close = path.find(']', position + 1);
            if (close == std::string_view::npos || !value->is_array())
                return nullptr;
            std::size_t index = 0;
            const auto first = path.data() + position + 1;
            const auto last = path.data() + close;
            const auto parsed = std::from_chars(first, last, index);
            if (parsed.ec != std::errc{} || parsed.ptr != last || index >= value->size())
                return nullptr;
            value = &(*value)[index];
            position = close + 1;
            continue;
        }
        std::size_t end = position;
        while (end < path.size() && path[end] != '.' && path[end] != '[')
            ++end;
        if (!value->is_object())
            return nullptr;
        const auto found = value->find(std::string(path.substr(position, end - position)));
        if (found == value->end())
            return nullptr;
        value = &*found;
        position = end;
    }
    return value;
}

std::string lower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

std::size_t editDistance(std::string_view lhs, std::string_view rhs) {
    const std::string a = lower(lhs);
    const std::string b = lower(rhs);
    std::vector<std::size_t> previous(b.size() + 1);
    std::vector<std::size_t> current(b.size() + 1);
    for (std::size_t column = 0; column <= b.size(); ++column)
        previous[column] = column;
    for (std::size_t row = 1; row <= a.size(); ++row) {
        current[0] = row;
        for (std::size_t column = 1; column <= b.size(); ++column) {
            const std::size_t substitution =
                previous[column - 1] + (a[row - 1] == b[column - 1] ? 0 : 1);
            current[column] =
                std::min({previous[column] + 1, current[column - 1] + 1, substitution});
        }
        previous.swap(current);
    }
    return previous.back();
}

std::string topLevelName(std::string_view key) {
    const std::size_t array = key.find("[]");
    const std::size_t dot = key.find('.');
    std::size_t end = key.size();
    if (array != std::string_view::npos)
        end = std::min(end, array);
    if (dot != std::string_view::npos)
        end = std::min(end, dot);
    return std::string(key.substr(0, end));
}

bool isArrayRoot(const std::vector<ParameterField> &fields, std::string_view root) {
    const std::string prefix = std::string(root) + "[].";
    return std::any_of(fields.begin(), fields.end(),
                       [&](const ParameterField &field) { return field.key.starts_with(prefix); });
}

const ParameterField *findDirectField(const std::vector<ParameterField> &fields,
                                      std::string_view key) {
    const auto found = std::find_if(fields.begin(), fields.end(),
                                    [&](const ParameterField &field) { return field.key == key; });
    return found == fields.end() ? nullptr : &*found;
}

const ParameterField *findArrayElementField(const std::vector<ParameterField> &fields,
                                            std::string_view root, std::string_view key) {
    const std::string full_key = std::string(root) + "[]." + std::string(key);
    return findDirectField(fields, full_key);
}

std::vector<std::string> arrayElementMembers(const std::vector<ParameterField> &fields,
                                             std::string_view root) {
    const std::string prefix = std::string(root) + "[].";
    std::vector<std::string> members;
    for (const auto &field : fields) {
        if (!field.key.starts_with(prefix))
            continue;
        const std::string member = field.key.substr(prefix.size());
        if (member.find_first_of(".[") == std::string::npos)
            members.push_back(member);
    }
    return members;
}

std::string arrayElementExpectation(const std::vector<std::string> &members) {
    std::string expected = "object with members";
    for (std::size_t index = 0; index < members.size(); ++index)
        expected += (index == 0 ? " " : ", ") + members[index];
    return expected;
}

std::vector<std::string> suggestions(const std::vector<ParameterField> &fields,
                                     std::string_view query, std::string_view array_root = {}) {
    std::set<std::string> unique;
    if (array_root.empty()) {
        for (const auto &field : fields)
            unique.insert(topLevelName(field.key));
    } else {
        const std::string prefix = std::string(array_root) + "[].";
        for (const auto &field : fields) {
            if (field.key.starts_with(prefix))
                unique.insert(field.key.substr(prefix.size()));
        }
    }
    std::vector<std::string> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end(), [&](const std::string &lhs, const std::string &rhs) {
        const std::size_t lhs_distance = editDistance(query, lhs);
        const std::size_t rhs_distance = editDistance(query, rhs);
        if (lhs_distance != rhs_distance)
            return lhs_distance < rhs_distance;
        return lhs < rhs;
    });
    if (result.size() > 5)
        result.resize(5);
    return result;
}

std::string enumExpected(const ParameterField &field) {
    std::string expected = "one of ";
    for (std::size_t index = 0; index < field.enum_values.size(); ++index) {
        if (index != 0)
            expected += ", ";
        expected += field.enum_values[index];
    }
    return expected;
}

bool integerFromJson(const Json &input, bool unsigned_slot, Json &converted) {
    if (!input.is_number())
        return false;

    if (unsigned_slot) {
        std::uint64_t value = 0;
        if (input.is_number_unsigned()) {
            value = input.get<std::uint64_t>();
        } else if (input.is_number_integer()) {
            const std::int64_t signed_value = input.get<std::int64_t>();
            if (signed_value < 0)
                return false;
            value = static_cast<std::uint64_t>(signed_value);
        } else {
            const double number = input.get<double>();
            const long double wide = static_cast<long double>(number);
            if (!std::isfinite(number) || std::trunc(number) != number || wide < 0.0L ||
                wide >= std::ldexp(1.0L, 64))
                return false;
            value = static_cast<std::uint64_t>(number);
        }
        converted = value;
        return true;
    }

    std::int64_t value = 0;
    if (input.is_number_unsigned()) {
        const std::uint64_t unsigned_value = input.get<std::uint64_t>();
        if (unsigned_value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return false;
        value = static_cast<std::int64_t>(unsigned_value);
    } else if (input.is_number_integer()) {
        value = input.get<std::int64_t>();
    } else {
        const double number = input.get<double>();
        const long double wide = static_cast<long double>(number);
        const long double min_value = -std::ldexp(1.0L, 63);
        const long double max_exclusive = std::ldexp(1.0L, 63);
        if (!std::isfinite(number) || std::trunc(number) != number || wide < min_value ||
            wide >= max_exclusive)
            return false;
        value = static_cast<std::int64_t>(number);
    }
    converted = value;
    return true;
}

bool convertValue(const ParameterField &field, const Json &input, const Json *current,
                  Json &converted, std::string &expected) {
    switch (field.kind) {
    case FieldKind::Number:
        if (current && isInteger(*current)) {
            expected = "integer";
            return integerFromJson(input, current->is_number_unsigned(), converted);
        }
        expected = "number";
        if (!input.is_number())
            return false;
        converted = input.get<double>();
        return std::isfinite(converted.get<double>());
    case FieldKind::Bool:
        expected = "boolean";
        if (!input.is_boolean())
            return false;
        converted = input;
        return true;
    case FieldKind::String:
    case FieldKind::FilePath:
        expected = "string";
        if (!input.is_string())
            return false;
        converted = input;
        return true;
    case FieldKind::Enum:
        expected = enumExpected(field);
        if (input.is_string()) {
            for (std::size_t index = 0; index < field.enum_values.size(); ++index) {
                if (input.get<std::string>() == field.enum_values[index]) {
                    converted = static_cast<std::int64_t>(index);
                    return true;
                }
            }
            return false;
        }
        if (!integerFromJson(input, false, converted))
            return false;
        const std::int64_t index = converted.get<std::int64_t>();
        return index >= 0 && static_cast<std::uint64_t>(index) < field.enum_values.size();
    }
    return false;
}

bool requestedMatchesStored(const RequestedScalar &requested, const Json &stored) {
    if (!requested.floating)
        return equalJson(requested.value, stored);
    if (!requested.value.is_number_float() || !stored.is_number_float())
        return false;
    const double expected = requested.value.get<double>();
    const double actual = stored.get<double>();
    const double tolerance = std::max(1e-12, 1e-6 * std::max(std::abs(expected), std::abs(actual)));
    return std::abs(expected - actual) <= tolerance;
}

bool isRequestedSubtree(std::string_view path, const std::vector<std::string> &roots) {
    for (const auto &root : roots) {
        if (path == root)
            return true;
        if (path.size() > root.size() && path.starts_with(root) &&
            (path[root.size()] == '.' || path[root.size()] == '['))
            return true;
    }
    return false;
}

void collectLeaves(const Json &value, const std::string &path,
                   std::map<std::string, Json> &leaves) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            const std::string child = path.empty() ? it.key() : path + "." + it.key();
            collectLeaves(it.value(), child, leaves);
        }
    } else if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index)
            collectLeaves(value[index], path + "[" + std::to_string(index) + "]", leaves);
    } else if (!path.empty()) {
        leaves.emplace(path, value);
    }
}

std::string exceptionText(std::exception_ptr exception) {
    try {
        if (exception)
            std::rethrow_exception(exception);
    } catch (const std::exception &error) {
        return error.what();
    } catch (...) {
        return "unknown exception";
    }
    return "unknown exception";
}

bool restore(IComponentEngine &engine, const Json &snapshot, std::string &error) {
    try {
        engine.deserialize(snapshot);
        return true;
    } catch (...) {
        error = exceptionText(std::current_exception());
        return false;
    }
}

} // namespace

bool ParamWriteResult::ok() const {
    return status == ParamWriteStatus::Applied || status == ParamWriteStatus::Unchanged;
}

ParamWriteResult applyComponentParams(IComponentEngine &engine,
                                      const std::vector<ParameterField> &state_fields,
                                      const OrderedJson &params) {
    if (!params.is_object())
        return failure(ParamWriteStatus::TypeMismatch, {}, "object");

    const Json before = engine.serialize();
    if (!before.is_object())
        return failure(ParamWriteStatus::TypeMismatch, {}, "object");

    Json merged = before;
    std::vector<RequestedScalar> requested_scalars;
    std::vector<std::string> requested_roots;

    for (auto it = params.begin(); it != params.end(); ++it) {
        const std::string &key = it.key();
        const OrderedJson &input = it.value();
        if (isPathParameter(key))
            return failure(ParamWriteStatus::PathParamUnsupported, key);

        const ParameterField *field = findDirectField(state_fields, key);
        const bool array_root = isArrayRoot(state_fields, key);
        if (!field && !array_root) {
            auto result = failure(ParamWriteStatus::UnknownKey, key);
            result.suggestions = suggestions(state_fields, key);
            return result;
        }
        if (field && field->read_only)
            return failure(ParamWriteStatus::ReadOnly, key);

        requested_roots.push_back(key);
        if (array_root) {
            if (!input.is_array())
                return failure(ParamWriteStatus::TypeMismatch, key, "array");
            Json normalized = Json::array();
            for (std::size_t index = 0; index < input.size(); ++index) {
                const OrderedJson &element = input[index];
                const std::string element_path = key + "[" + std::to_string(index) + "]";
                if (!element.is_object())
                    return failure(ParamWriteStatus::TypeMismatch, element_path, "object");
                Json normalized_element = Json::object();
                for (auto element_it = element.begin(); element_it != element.end(); ++element_it) {
                    const std::string &element_key = element_it.key();
                    const std::string path = element_path + "." + element_key;
                    if (isPathParameter(element_key))
                        return failure(ParamWriteStatus::PathParamUnsupported, path);
                    const ParameterField *element_field =
                        findArrayElementField(state_fields, key, element_key);
                    if (!element_field) {
                        auto result = failure(ParamWriteStatus::UnknownKey, path);
                        result.suggestions = suggestions(state_fields, element_key, key);
                        return result;
                    }
                    if (element_field->read_only)
                        return failure(ParamWriteStatus::ReadOnly, path);
                    const Json *current = valueAtPath(before, path);
                    Json converted;
                    std::string expected;
                    const Json element_input = element_it.value();
                    if (!convertValue(*element_field, element_input, current, converted, expected))
                        return failure(ParamWriteStatus::TypeMismatch, path, std::move(expected));
                    normalized_element[element_key] = converted;
                    requested_scalars.push_back(
                        {path, element_input, converted, converted.is_number_float()});
                }
                const auto members = arrayElementMembers(state_fields, key);
                for (const auto &member : members) {
                    if (!element.contains(member))
                        return failure(ParamWriteStatus::TypeMismatch, element_path,
                                       arrayElementExpectation(members));
                }
                normalized.push_back(std::move(normalized_element));
            }
            merged[key] = std::move(normalized);
            continue;
        }
        const Json input_value = input;

        const Json *current = valueAtPath(before, key);
        Json converted;
        std::string expected;
        if (!convertValue(*field, input_value, current, converted, expected))
            return failure(ParamWriteStatus::TypeMismatch, key, std::move(expected));
        merged[key] = converted;
        requested_scalars.push_back({key, input_value, converted, converted.is_number_float()});
    }

    if (equalJson(before, merged))
        return failure(ParamWriteStatus::Unchanged);

    try {
        engine.deserialize(merged);
    } catch (...) {
        const std::string deserialize_error = exceptionText(std::current_exception());
        std::string restore_error;
        auto result = failure(ParamWriteStatus::DeserializeFailed);
        if (!restore(engine, before, restore_error)) {
            result.status = ParamWriteStatus::RestoreFailed;
            result.error = restore_error;
        } else {
            result.error = deserialize_error;
        }
        return result;
    }

    const Json after = engine.serialize();
    for (const auto &requested : requested_scalars) {
        const Json *stored = valueAtPath(after, requested.path);
        if (stored && requestedMatchesStored(requested, *stored))
            continue;

        auto result = failure(ParamWriteStatus::EngineAdjusted, requested.path);
        result.requested = requested.input;
        result.stored = stored ? *stored : Json(nullptr);
        std::string restore_error;
        if (!restore(engine, before, restore_error)) {
            result.status = ParamWriteStatus::RestoreFailed;
            result.error = restore_error;
        }
        return result;
    }

    std::map<std::string, Json> old_leaves;
    std::map<std::string, Json> new_leaves;
    collectLeaves(before, {}, old_leaves);
    collectLeaves(after, {}, new_leaves);
    std::set<std::string> paths;
    for (const auto &[path, _] : old_leaves)
        paths.insert(path);
    for (const auto &[path, _] : new_leaves)
        paths.insert(path);

    ParamWriteResult result;
    result.status = ParamWriteStatus::Applied;
    for (const auto &path : paths) {
        if (isRequestedSubtree(path, requested_roots))
            continue;
        const auto old_value = old_leaves.find(path);
        const auto new_value = new_leaves.find(path);
        const bool old_present = old_value != old_leaves.end();
        const bool new_present = new_value != new_leaves.end();
        if (old_present == new_present &&
            (!old_present || equalJson(old_value->second, new_value->second)))
            continue;
        result.also_changed.push_back({path, old_present ? old_value->second : Json(nullptr),
                                       new_present ? new_value->second : Json(nullptr)});
    }
    return result;
}
