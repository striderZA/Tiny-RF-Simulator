#pragma once

#include <optional>
#include <string>
#include <string_view>

std::optional<std::string> generateAgentToken();
bool agentTokensEqual(std::string_view left, std::string_view right);
