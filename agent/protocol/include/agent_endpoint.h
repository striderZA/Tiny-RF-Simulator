#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct AgentEndpoint {
    int port = 0;
    std::string bridge_token, gui_token;
    long long pid = 0;
    std::string app_version;
    int catalog_version = 0;
};

std::string fnv1a64Hex(std::string_view bytes);
std::filesystem::path currentExecutableDirectory();
std::string agentInstallKey(const std::filesystem::path &exe_dir);
std::optional<std::filesystem::path> defaultAgentEndpointDirectory(std::string *error);
std::filesystem::path agentEndpointFileName(const std::string &install_key);
bool ensurePrivateDirectory(const std::filesystem::path &dir, std::string *error);
bool writeAgentEndpoint(const std::filesystem::path &file, const AgentEndpoint &,
                        std::string *error);
std::optional<AgentEndpoint> readAgentEndpoint(const std::filesystem::path &file,
                                               std::string *error);
bool removeAgentEndpointIfOwned(const std::filesystem::path &file, std::string_view gui_token);
