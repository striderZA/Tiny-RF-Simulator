#pragma once

#include "agent_wire.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class AgentReadStatus { Line, Timeout, Closed, Oversized, Error };

class AgentChannel {
  public:
    AgentChannel(const AgentChannel &) = delete;
    AgentChannel &operator=(const AgentChannel &) = delete;
    AgentChannel(AgentChannel &&other) noexcept;
    AgentChannel &operator=(AgentChannel &&other) noexcept;
    ~AgentChannel();

    AgentReadStatus readLine(std::string &line, std::chrono::milliseconds timeout);
    bool writeLine(std::string_view line);
    bool writeLine(std::string_view line, std::chrono::milliseconds timeout);
    void close();
    bool isOpen() const;

  private:
    explicit AgentChannel(std::intptr_t socket) noexcept;

    friend class AgentListener;
    friend std::optional<AgentChannel>
    connectAgentLoopback(int port, std::chrono::milliseconds timeout, std::string *error);

    std::intptr_t m_socket{-1};
    AgentLineReader m_reader;
};

class AgentListener {
  public:
    AgentListener(const AgentListener &) = delete;
    AgentListener &operator=(const AgentListener &) = delete;
    AgentListener(AgentListener &&other) noexcept;
    AgentListener &operator=(AgentListener &&other) noexcept;
    ~AgentListener();

    static std::optional<AgentListener> bindLoopback(std::string *error);
    int port() const;
    std::optional<AgentChannel> accept(std::chrono::milliseconds timeout);
    void close();

  private:
    AgentListener(std::intptr_t socket, int port) noexcept;

    std::intptr_t m_socket{-1};
    int m_port{0};
};

std::optional<AgentChannel> connectAgentLoopback(int port, std::chrono::milliseconds timeout,
                                                 std::string *error);
