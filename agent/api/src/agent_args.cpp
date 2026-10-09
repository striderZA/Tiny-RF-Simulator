#include "agent_args.h"

#include <limits>
#include <string>
#include <utility>

namespace {

std::string argumentPath(std::string_view key) {
    std::string path{"/"};
    for (const char character : key) {
        if (character == '~')
            path += "~0";
        else if (character == '/')
            path += "~1";
        else
            path += character;
    }
    return path;
}

[[noreturn]] void invalidArgument(std::string_view path, std::string message) {
    AgentError error{AgentErrorCode::InvalidArgument, std::move(message)};
    error.details = {{"path", std::string(path)}};
    throw AgentArgumentError(std::move(error));
}

std::uint64_t unsignedInteger(const nlohmann::ordered_json &value, std::string_view path) {
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if (number >= 0)
            return static_cast<std::uint64_t>(number);
    }
    invalidArgument(path, "expected a non-negative integer");
}

} // namespace

AgentArgumentError::AgentArgumentError(AgentError error) : m_error(std::move(error)) {}

const char *AgentArgumentError::what() const noexcept { return m_error.message.c_str(); }

AgentArgs::AgentArgs(const nlohmann::ordered_json &arguments,
                     std::initializer_list<std::string_view> allowed_keys)
    : m_arguments(arguments) {
    if (!m_arguments.is_object())
        invalidArgument("/", "expected an object");

    for (auto it = m_arguments.begin(); it != m_arguments.end(); ++it) {
        bool allowed = false;
        for (const auto key : allowed_keys) {
            if (it.key() == key) {
                allowed = true;
                break;
            }
        }
        if (!allowed)
            invalidArgument(argumentPath(it.key()), "unknown argument");
    }
}

std::string AgentArgs::optionalString(std::string_view key, std::string default_value) const {
    const auto it = m_arguments.find(std::string(key));
    if (it == m_arguments.end())
        return default_value;
    if (!it->is_string())
        invalidArgument(argumentPath(key), "expected a string");
    return it->get<std::string>();
}

int AgentArgs::optionalInt(std::string_view key, int default_value, int minimum,
                           int maximum) const {
    const auto it = m_arguments.find(std::string(key));
    if (it == m_arguments.end())
        return default_value;
    const std::string path = argumentPath(key);
    const std::uint64_t number = unsignedInteger(*it, path);
    if (number > static_cast<std::uint64_t>(maximum) ||
        number < static_cast<std::uint64_t>(minimum))
        invalidArgument(path, "integer is out of range");
    return static_cast<int>(number);
}

std::uint64_t AgentArgs::requiredUInt64(std::string_view key) const {
    const auto it = m_arguments.find(std::string(key));
    const std::string path = argumentPath(key);
    if (it == m_arguments.end())
        invalidArgument(path, "required argument is missing");
    return unsignedInteger(*it, path);
}

int AgentArgs::requiredInt(std::string_view key) const {
    const std::uint64_t number = requiredUInt64(key);
    if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        invalidArgument(argumentPath(key), "integer is out of range");
    return static_cast<int>(number);
}

bool AgentArgs::optionalBool(std::string_view key, bool default_value) const {
    const auto it = m_arguments.find(std::string(key));
    if (it == m_arguments.end())
        return default_value;
    if (!it->is_boolean())
        invalidArgument(argumentPath(key), "expected a boolean");
    return it->get<bool>();
}
