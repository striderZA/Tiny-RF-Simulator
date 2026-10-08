#pragma once

#include "agent_errors.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <initializer_list>
#include <string>
#include <string_view>

class AgentArgumentError final : public std::exception {
  public:
    explicit AgentArgumentError(AgentError error);
    const char *what() const noexcept override;
    const AgentError &error() const { return m_error; }

  private:
    AgentError m_error;
};

class AgentArgs final {
  public:
    AgentArgs(const nlohmann::ordered_json &arguments,
              std::initializer_list<std::string_view> allowed_keys);

    std::uint64_t requiredUInt64(std::string_view key) const;
    int requiredInt(std::string_view key) const;
    bool optionalBool(std::string_view key, bool default_value) const;

  private:
    const nlohmann::ordered_json &m_arguments;
};
