#pragma once

#include "agent_errors.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

struct GuiLinkTimeouts {
    std::chrono::milliseconds connect{2000};
    std::chrono::milliseconds hello{5000};
    std::chrono::milliseconds call{120000};
};

class GuiLink {
  public:
    GuiLink(std::filesystem::path endpoint_file, GuiLinkTimeouts timeouts);
    ~GuiLink();

    GuiLink(const GuiLink &) = delete;
    GuiLink &operator=(const GuiLink &) = delete;
    GuiLink(GuiLink &&) noexcept;
    GuiLink &operator=(GuiLink &&) noexcept;

    void setClientInfo(std::string name, std::string version);
    AgentToolResult call(const std::string &tool, const nlohmann::ordered_json &arguments);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
