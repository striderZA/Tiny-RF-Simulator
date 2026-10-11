#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "agent_catalog.h"
#include "agent_endpoint.h"
#include "agent_socket.h"
#include "agent_wire.h"
#include "test_temp_paths.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

// A real rfsim-agent/1 peer, intentionally independent of Task 16's test bridge.
class FakeGui {
  public:
    using Json = nlohmann::json;

    struct Reply {
        Json message;
        bool close_after = false;
    };
    using Script = std::function<std::optional<Reply>(const Json &)>;

    explicit FakeGui(std::string gui_token = std::string(64, 'c'),
                     int catalog_version = kAgentCatalogVersion)
        : m_gui_token(std::move(gui_token)), m_catalog_version(catalog_version) {
        static std::atomic<unsigned long long> next{0};
        m_directory = std::filesystem::temp_directory_path() /
                      ("rfsim-fake-gui-" + test_temp_paths::processTag() + "-" +
                       std::to_string(next.fetch_add(1)));
        std::string error;

        if (!ensurePrivateDirectory(m_directory, &error))
            throw std::runtime_error("FakeGui directory: " + error);
        auto listener = AgentListener::bindLoopback(&error);
        if (!listener)
            throw std::runtime_error("FakeGui listener: " + error);
        m_listener.emplace(std::move(*listener));
        m_endpoint_file = m_directory / "agent-endpoint.json";
        AgentEndpoint endpoint;
        endpoint.port = m_listener->port();
        endpoint.bridge_token = std::string(64, 'b');
        endpoint.gui_token = m_gui_token;
        endpoint.pid = 1;
        endpoint.app_version = "test-gui";
        endpoint.catalog_version = m_catalog_version;
        if (!writeAgentEndpoint(m_endpoint_file, endpoint, &error))
            throw std::runtime_error("FakeGui endpoint: " + error);
        m_endpoint = endpoint;
        m_thread = std::thread([this] { serve(); });
    }

    FakeGui(const FakeGui &) = delete;
    FakeGui &operator=(const FakeGui &) = delete;
    ~FakeGui() {
        m_stopping = true;
        if (m_thread.joinable())
            m_thread.join();
        if (m_listener)
            m_listener->close();
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
    }

    std::filesystem::path endpointFile() const { return m_endpoint_file; }
    AgentEndpoint endpoint() const { return m_endpoint; }
    unsigned helloCount() const { return m_hello_count.load(); }
    bool authenticated() const { return m_authenticated.load(); }
    void setScript(Script script) {
        std::lock_guard lock(m_mutex);
        m_script = std::move(script);
    }
    void setHelloScript(Script script) {
        std::lock_guard lock(m_mutex);
        m_hello_script = std::move(script);
    }
    void holdReplies(bool hold = true) { m_hold_replies = hold; }
    void holdHelloReply(bool hold = true) { m_hold_hello_reply = hold; }
    void closeAfterCall(bool close = true) { m_close_after_call = close; }
    Json helloRequest() const {
        std::lock_guard lock(m_mutex);
        return m_hello_request;
    }
    Json lastCall() const {
        std::lock_guard lock(m_mutex);
        return m_last_call;
    }

  private:
    void waitForClose(AgentChannel &client) {
        while (!m_stopping) {
            std::string discarded;
            const auto status = client.readLine(discarded, std::chrono::milliseconds(20));
            if (status == AgentReadStatus::Timeout)
                continue;
            break;
        }
    }

    void serve() {
        while (!m_stopping) {
            auto client = m_listener->accept(std::chrono::milliseconds(20));
            if (!client) {
                if (m_stopping)
                    break;
                continue;
            }
            std::string line;
            const auto hello_status = client->readLine(line, std::chrono::seconds(2));
            if (hello_status != AgentReadStatus::Line) {
                if (m_stopping)
                    break;
                continue;
            }
            Json hello;
            try {
                hello = Json::parse(line);
            } catch (...) {
                continue;
            }
            {
                std::lock_guard lock(m_mutex);
                m_hello_request = hello;
            }
            const auto &params = hello.at("params");
            if (hello.value("method", std::string{}) != "hello" ||
                params.value("protocol", std::string{}) != kAgentWireProtocol ||
                params.value("bridge_token", std::string{}) != m_endpoint.bridge_token)
                continue;
            ++m_hello_count;
            m_authenticated = true;
            if (m_hold_hello_reply) {
                waitForClose(*client);
                continue;
            }
            Script hello_script;
            {
                std::lock_guard lock(m_mutex);
                hello_script = m_hello_script;
            }
            std::optional<Reply> hello_reply;
            if (hello_script)
                hello_reply = hello_script(hello);
            else
                hello_reply = Reply{Json{{"jsonrpc", "2.0"},
                                         {"id", hello.value("id", Json())},
                                         {"result",
                                          {{"gui_token", m_gui_token},
                                           {"app_version", "test-gui"},
                                           {"catalog_version", m_catalog_version},
                                           {"epoch", 1}}}}};
            if (!hello_reply || !client->writeLine(hello_reply->message.dump()) ||
                hello_reply->close_after)
                continue;
            while (!m_stopping) {
                const auto status = client->readLine(line, std::chrono::milliseconds(20));
                if (status == AgentReadStatus::Timeout)
                    continue;
                if (status != AgentReadStatus::Line)
                    break;
                Json request;
                try {
                    request = Json::parse(line);
                } catch (...) {
                    break;
                }
                {
                    std::lock_guard lock(m_mutex);
                    m_last_call = request;
                }
                if (m_close_after_call)
                    break;
                if (m_hold_replies)
                    continue;
                Script script;
                {
                    std::lock_guard lock(m_mutex);
                    script = m_script;
                }
                std::optional<Reply> reply;
                if (script)
                    reply = script(request);
                else
                    reply = Reply{Json{{"jsonrpc", "2.0"},
                                       {"id", request.value("id", Json())},
                                       {"result",
                                        {{"is_error", false},
                                         {"structured", {{"ok", true}}},
                                         {"text", "{\"ok\":true}"}}}}};
                if (!reply)
                    continue;
                if (!client->writeLine(reply->message.dump()) || reply->close_after)
                    break;
            }
        }
    }

    std::filesystem::path m_directory, m_endpoint_file;
    std::optional<AgentListener> m_listener;
    AgentEndpoint m_endpoint;
    std::string m_gui_token;
    int m_catalog_version;
    std::atomic<bool> m_stopping{false}, m_authenticated{false};
    std::atomic<unsigned> m_hello_count{0};
    std::atomic<bool> m_hold_replies{false}, m_hold_hello_reply{false}, m_close_after_call{false};
    mutable std::mutex m_mutex;
    Script m_script;
    Script m_hello_script;
    Json m_hello_request = Json::object(), m_last_call = Json::object();
    std::thread m_thread;
};
