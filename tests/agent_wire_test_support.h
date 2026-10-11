#pragma once

#include "agent_catalog.h"
#include "agent_endpoint.h"
#include "agent_socket.h"
#include "agent_wire.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

class TestBridgeClient {
  public:
    static std::optional<TestBridgeClient> connect(const std::filesystem::path &endpoint_file,
                                                   std::string *error = nullptr) {
        auto endpoint = readAgentEndpoint(endpoint_file, error);
        if (!endpoint) {
            return std::nullopt;
        }
        auto channel = connectAgentLoopback(endpoint->port, std::chrono::seconds{2}, error);
        if (!channel) {
            return std::nullopt;
        }
        TestBridgeClient client(std::move(*channel), std::move(*endpoint));
        return client;
    }

    TestBridgeClient(const TestBridgeClient &) = delete;
    TestBridgeClient &operator=(const TestBridgeClient &) = delete;
    TestBridgeClient(TestBridgeClient &&) noexcept = default;
    TestBridgeClient &operator=(TestBridgeClient &&) noexcept = default;

    bool hello(std::string name = "test-bridge", std::string version = "1.0",
               std::string protocol = std::string{kAgentWireProtocol},
               int catalog_version = kAgentCatalogVersion) {
        const auto id = ++m_next_id;
        if (!send({{"jsonrpc", "2.0"},
                   {"id", id},
                   {"method", "hello"},
                   {"params",
                    {{"protocol", std::move(protocol)},
                     {"bridge_token", m_endpoint.bridge_token},
                     {"catalog_version", catalog_version},
                     {"client", {{"name", std::move(name)}, {"version", std::move(version)}}}}}})) {
            return false;
        }
        auto response = receive();
        if (!response || !response->contains("result") || !response->at("result").is_object()) {
            m_last_response = std::move(response);
            return false;
        }
        m_gui_token = response->at("result").value("gui_token", std::string{});
        m_last_response = std::move(response);
        return !m_gui_token.empty();
    }

    bool call(std::string tool,
              nlohmann::ordered_json arguments = nlohmann::ordered_json::object()) {
        const auto id = ++m_next_id;
        return send({{"jsonrpc", "2.0"},
                     {"id", id},
                     {"method", "call"},
                     {"params", {{"tool", std::move(tool)}, {"arguments", std::move(arguments)}}}});
    }

    std::optional<nlohmann::json> receive(std::chrono::milliseconds timeout = std::chrono::seconds{
                                              2}) {
        std::string line;
        const auto result = m_channel.readLine(line, timeout);
        if (result != AgentReadStatus::Line) {
            return std::nullopt;
        }
        try {
            return nlohmann::json::parse(line);
        } catch (...) {
            return std::nullopt;
        }
    }
    AgentReadStatus readStatus(std::string &line,
                               std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
        return m_channel.readLine(line, timeout);
    }

    bool sendRawLine(std::string_view line) { return m_channel.writeLine(line); }

    void close() { m_channel.close(); }
    bool isOpen() const { return m_channel.isOpen(); }
    const AgentEndpoint &endpoint() const { return m_endpoint; }
    const std::string &guiToken() const { return m_gui_token; }
    const std::optional<nlohmann::json> &lastResponse() const { return m_last_response; }

  private:
    TestBridgeClient(AgentChannel channel, AgentEndpoint endpoint)
        : m_channel(std::move(channel)), m_endpoint(std::move(endpoint)) {}

    bool send(const nlohmann::json &message) { return m_channel.writeLine(message.dump()); }

    AgentChannel m_channel;
    AgentEndpoint m_endpoint;
    int m_next_id = 0;
    std::string m_gui_token;
    std::optional<nlohmann::json> m_last_response;
};