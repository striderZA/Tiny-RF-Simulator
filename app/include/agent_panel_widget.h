#pragma once

#include "agent_server.h"
#include "app_agent_host.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

struct AgentPanelView {
    bool server_on = false;
    AgentServerStatus status;
    std::string bridge_path;
    const std::deque<AgentActivity> *activity = nullptr;
    const std::deque<AgentCheckpoint> *checkpoints = nullptr;
};

class AgentPanelWidget {
  public:
    std::function<void(bool)> onServerToggled;
    std::function<void(std::uint64_t)> onRevert;

    void draw(const char *title, bool *open, const AgentPanelView &view);

  private:
    std::optional<std::uint64_t> m_pending_checkpoint_id;
};
