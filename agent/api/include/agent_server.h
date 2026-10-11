#pragma once

#include "agent_api.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

struct AgentServerConfig {
    std::filesystem::path endpoint_file;
    std::string app_version;
    std::chrono::milliseconds pump_budget{8};
    std::chrono::milliseconds park_timeout{30000};
    std::chrono::milliseconds hello_timeout{5000};
    std::function<std::chrono::steady_clock::time_point()> now = [] {
        return std::chrono::steady_clock::now();
    };
};

enum class AgentServerState { Off, Waiting, Connected, Busy, Error };

struct AgentServerStatus {
    AgentServerState state = AgentServerState::Off;
    std::string client_name;
    std::string client_version;
    std::string error;
};

class AgentServer {
  public:
    using Completion = std::function<void(const AgentToolResult &)>;

    AgentServer(IAgentCallExecutor &executor, IAgentHost &host, AgentServerConfig config);
    ~AgentServer();

    AgentServer(const AgentServer &) = delete;
    AgentServer &operator=(const AgentServer &) = delete;

    bool start(std::string *error);
    void stop();
    bool running() const;
    void submit(AgentCall call, Completion done);
    void pump();
    AgentServerStatus status() const;
    const AgentServerConfig &config() const;

  private:
    struct Impl;

    AgentServerConfig m_config;
    std::unique_ptr<Impl> m_impl;
};
