#include "agent_server.h"

#include "agent_api.h"
#include "agent_catalog.h"
#include "agent_endpoint.h"
#include "agent_socket.h"
#include "agent_token.h"
#include "agent_wire.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

using AgentJson = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;
using SteadyClock = std::chrono::steady_clock;
using TimePoint = SteadyClock::time_point;

bool validRpcId(const OrderedJson &id) { return id.is_string() || id.is_number(); }

AgentJson rpcError(const AgentJson &id, std::string code, std::string message) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"error",
             {{"code", kAgentRpcErrorCode},
              {"message", std::move(message)},
              {"data", {{"code", std::move(code)}}}}}};
}

constexpr auto kAgentWriteTimeout = std::chrono::milliseconds{5000};

bool writeAgentMessage(AgentChannel &channel, const AgentJson &message) {
    try {
        const std::string line = message.dump();
        return line.size() <= kAgentMaxLineBytes && channel.writeLine(line, kAgentWriteTimeout);
    } catch (...) {
        return false;
    }
}

AgentJson rpcCallResult(const AgentJson &id, const AgentToolResult &result) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"result",
             {{"is_error", result.is_error},
              {"structured", result.structured},
              {"text", result.structured.dump()}}}};
}

std::optional<AgentJson> boundedRpcCallResult(const AgentJson &id, const AgentToolResult &result,
                                              std::optional<std::uint64_t> epoch) {
    const auto encode_if_bounded =
        [&id](const AgentToolResult &candidate) -> std::optional<AgentJson> {
        try {
            AgentJson response = rpcCallResult(id, candidate);
            if (response.dump().size() <= kAgentMaxLineBytes)
                return response;
        } catch (...) {
            // Keep serializer details out of client-facing errors.
        }
        return std::nullopt;
    };

    if (auto response = encode_if_bounded(result))
        return response;

    const AgentToolResult error = agentErrorResult(
        AgentError{AgentErrorCode::Internal, "Agent tool result exceeds the 1 MiB protocol limit"},
        epoch);
    return encode_if_bounded(error);
}

AgentToolResult unavailableResult(std::optional<std::uint64_t> epoch) {
    return agentErrorResult(
        AgentError{AgentErrorCode::SimulatorUnavailable, "RF Simulator is unavailable"}, epoch);
}

void invokeCompletion(const AgentServer::Completion &done, const AgentToolResult &result) {
    if (!done)
        return;
    try {
        done(result);
    } catch (...) {
        // Completion handlers are caller-owned and must not unwind the UI pump or shutdown.
    }
}

long long currentProcessId() {
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

} // namespace

struct AgentServer::Impl {
    struct PendingCall {
        AgentCall call;
        Completion done;
        bool has_request = false;
        std::uint64_t session = 0;
        AgentJson id = nullptr;
        std::optional<TimePoint> parked_since;
    };

    struct OutgoingResponse {
        std::uint64_t session = 0;
        AgentJson message;
        bool close_connection = false;
    };

    Impl(IAgentCallExecutor &call_executor, IAgentHost &agent_host,
         const AgentServerConfig &server_config)
        : executor(call_executor), host(agent_host), config(server_config) {}

    IAgentCallExecutor &executor;
    IAgentHost &host;
    const AgentServerConfig &config;

    mutable std::mutex mutex;
    std::mutex lifecycle_mutex;
    std::mutex pump_mutex;
    std::deque<PendingCall> inbox;
    std::deque<OutgoingResponse> outbox;
    std::thread listener_thread;
    AgentServerStatus current_status;
    std::string owned_gui_token;
    std::uint64_t next_session = 0;
    std::uint64_t active_session = 0;
    std::uint64_t authenticated_session = 0;
    std::optional<std::uint64_t> current_epoch;
    bool is_running = false;
    bool stopping = false;
    bool stop_requested = false;
    bool endpoint_written = false;

    bool running() const {
        std::lock_guard lock(mutex);
        return is_running && !stopping;
    }

    AgentServerStatus status() const {
        std::lock_guard lock(mutex);
        return current_status;
    }

    std::optional<std::uint64_t> epochSnapshotLocked() const { return current_epoch; }

    void refreshEpoch() {
        try {
            const std::uint64_t value = executor.epoch();
            std::lock_guard lock(mutex);
            current_epoch = value;
        } catch (...) {
            // Keep the last epoch snapshot. Never expose executor exceptions on the wire.
        }
    }

    TimePoint now() const {
        try {
            return config.now ? config.now() : SteadyClock::now();
        } catch (...) {
            return SteadyClock::now();
        }
    }

    AgentToolResult unavailableResultLocked() const {
        return ::unavailableResult(epochSnapshotLocked());
    }

    void setStatusFromWorkLocked() {
        if (stopping)
            return;
        if (!inbox.empty()) {
            current_status.state = AgentServerState::Busy;
            return;
        }
        if (!is_running) {
            if (current_status.state != AgentServerState::Error)
                current_status = {};
            return;
        }
        if (authenticated_session != 0) {
            current_status.state = AgentServerState::Connected;
            return;
        }
        current_status = {AgentServerState::Waiting, {}, {}, {}};
    }

    bool start(std::string *error) {
        std::lock_guard lifecycle_lock(lifecycle_mutex);
        if (error)
            error->clear();
        {
            std::lock_guard lock(mutex);
            if (is_running && !stopping)
                return true;
        }

        if (listener_thread.joinable())
            listener_thread.join();
        removeOwnedEndpoint();

        std::string bind_error;
        auto listener = AgentListener::bindLoopback(&bind_error);
        if (!listener) {
            failStart(error, bind_error.empty() ? "could not bind agent listener" : bind_error);
            return false;
        }

        auto bridge = generateAgentToken();
        auto gui = generateAgentToken();
        if (!bridge || !gui) {
            failStart(error, "could not generate agent session tokens");
            return false;
        }
        while (agentTokensEqual(*bridge, *gui)) {
            gui = generateAgentToken();
            if (!gui) {
                failStart(error, "could not generate agent session tokens");
                return false;
            }
        }

        AgentEndpoint endpoint;
        endpoint.port = listener->port();
        endpoint.bridge_token = *bridge;
        endpoint.gui_token = *gui;
        endpoint.pid = currentProcessId();
        endpoint.app_version = config.app_version;
        endpoint.catalog_version = kAgentCatalogVersion;
        std::string endpoint_error;
        if (!writeAgentEndpoint(config.endpoint_file, endpoint, &endpoint_error)) {
            failStart(error,
                      endpoint_error.empty() ? "could not write agent endpoint" : endpoint_error);
            return false;
        }

        {
            std::lock_guard lock(mutex);
            owned_gui_token = *gui;
            endpoint_written = true;
            is_running = true;
            stopping = false;
            stop_requested = false;
            active_session = 0;
            authenticated_session = 0;
            current_status = {AgentServerState::Waiting, {}, {}, {}};
            setStatusFromWorkLocked();
        }

        try {
            AgentListener thread_listener = std::move(*listener);
            listener_thread =
                std::thread([this, thread_listener = std::move(thread_listener),
                             bridge = std::move(*bridge), gui = std::move(*gui)]() mutable {
                    listenerLoop(std::move(thread_listener), std::move(bridge), std::move(gui));
                });
        } catch (...) {
            {
                std::lock_guard lock(mutex);
                is_running = false;
                current_status = {
                    AgentServerState::Error, {}, {}, "could not start agent listener thread"};
            }
            if (error)
                *error = "could not start agent listener thread";
            removeOwnedEndpoint();
            return false;
        }
        return true;
    }

    void failStart(std::string *error, const std::string &message) {
        std::lock_guard lock(mutex);
        is_running = false;
        stopping = false;
        stop_requested = false;
        current_status = {AgentServerState::Error, {}, {}, message};
        if (error)
            *error = message;
    }

    void removeOwnedEndpoint() {
        std::string token;
        bool should_remove = false;
        {
            std::lock_guard lock(mutex);
            token = owned_gui_token;
            should_remove = endpoint_written && !token.empty();
            endpoint_written = false;
            owned_gui_token.clear();
        }
        if (should_remove)
            (void)removeAgentEndpointIfOwned(config.endpoint_file, token);
    }

    void stop() {
        std::unique_lock lifecycle_lock(lifecycle_mutex);
        {
            std::lock_guard lock(mutex);
            stopping = true;
            is_running = false;
        }

        std::vector<std::pair<Completion, AgentToolResult>> completions;
        {
            std::lock_guard pump_lock(pump_mutex);
            std::lock_guard lock(mutex);
            while (!inbox.empty()) {
                PendingCall pending = std::move(inbox.front());
                inbox.pop_front();
                AgentToolResult result = unavailableResultLocked();
                if (pending.has_request) {
                    outbox.push_back(
                        responseForCall(pending.session, pending.id, result, current_epoch));
                }
                if (pending.done)
                    completions.emplace_back(std::move(pending.done), std::move(result));
            }
            stop_requested = true;
        }

        if (listener_thread.joinable())
            listener_thread.join();
        removeOwnedEndpoint();

        {
            std::lock_guard lock(mutex);
            active_session = 0;
            authenticated_session = 0;
            stopping = false;
            stop_requested = false;
            current_status = {};
        }
        lifecycle_lock.unlock();
        for (const auto &[done, result] : completions)
            invokeCompletion(done, result);
    }

    void submit(AgentCall call, Completion done) {
        std::optional<AgentToolResult> rejected;
        {
            std::lock_guard lock(mutex);
            if (stopping) {
                rejected = unavailableResultLocked();
            } else {
                inbox.push_back(
                    {std::move(call), std::move(done), false, 0, nullptr, std::nullopt});
                setStatusFromWorkLocked();
            }
        }
        if (rejected)
            invokeCompletion(done, *rejected);
    }

    void enqueueNetworkCall(AgentCall call, std::uint64_t session, AgentJson id) {
        std::lock_guard lock(mutex);
        if (stopping) {
            AgentToolResult result = unavailableResultLocked();
            outbox.push_back(responseForCall(session, id, result, current_epoch));
            return;
        }
        inbox.push_back({std::move(call), {}, true, session, std::move(id), std::nullopt});
        setStatusFromWorkLocked();
    }

    OutgoingResponse responseForCall(std::uint64_t session, const AgentJson &id,
                                     const AgentToolResult &result,
                                     std::optional<std::uint64_t> epoch) const {
        auto message = boundedRpcCallResult(id, result, epoch);
        if (!message)
            return {session, nullptr, true};
        return {session, std::move(*message), false};
    }

    void queueResult(const PendingCall &pending, const AgentToolResult &result) {
        if (!pending.has_request)
            return;
        OutgoingResponse response =
            responseForCall(pending.session, pending.id, result, epochSnapshot());
        std::lock_guard lock(mutex);
        outbox.push_back(std::move(response));
    }

    void finishCall(PendingCall pending, AgentToolResult result,
                    std::vector<std::pair<Completion, AgentToolResult>> &completions) {
        queueResult(pending, result);
        if (pending.done)
            completions.emplace_back(std::move(pending.done), std::move(result));
    }

    void pump() {
        std::unique_lock pump_lock(pump_mutex);
        std::vector<std::pair<Completion, AgentToolResult>> completions;
        {
            std::lock_guard lock(mutex);
            if (stopping)
                return;
        }

        refreshEpoch();
        const TimePoint pump_started = now();
        std::size_t completed = 0;
        for (;;) {
            if (completed != 0 && now() - pump_started >= config.pump_budget)
                break;
            {
                std::lock_guard lock(mutex);
                if (stopping || inbox.empty())
                    break;
            }
            bool modal_open = false;
            try {
                modal_open = host.appModalOpen();
            } catch (...) {
                PendingCall failed;
                {
                    std::lock_guard lock(mutex);
                    if (inbox.empty() || stopping)
                        break;
                    failed = std::move(inbox.front());
                    inbox.pop_front();
                }
                AgentToolResult result =
                    agentErrorResult(AgentError{AgentErrorCode::Internal,
                                                "could not inspect simulator dialog state"},
                                     epochSnapshot());
                finishCall(std::move(failed), std::move(result), completions);
                ++completed;
                continue;
            }

            const TimePoint observed_at = now();
            PendingCall pending;
            bool timed_out = false;
            bool parked = false;
            {
                std::lock_guard lock(mutex);
                if (stopping || inbox.empty())
                    break;
                if (modal_open) {
                    for (PendingCall &queued : inbox) {
                        if (!queued.parked_since)
                            queued.parked_since = observed_at;
                    }
                }
                PendingCall &front = inbox.front();
                timed_out =
                    front.parked_since && observed_at - *front.parked_since > config.park_timeout;
                if (modal_open && !timed_out) {
                    parked = true;
                } else {
                    pending = std::move(front);
                    inbox.pop_front();
                }
            }

            if (parked)
                break;
            if (timed_out) {
                AgentToolResult result = agentErrorResult(
                    AgentError{
                        AgentErrorCode::Busy,
                        "RF Simulator is showing a dialog; ask the user to close it, then retry"},
                    epochSnapshot());
                finishCall(std::move(pending), std::move(result), completions);
                ++completed;
                continue;
            }

            AgentToolResult result;
            try {
                result = executor.execute(pending.call);
            } catch (...) {
                result = agentErrorResult(AgentError{AgentErrorCode::Internal,
                                                     "internal error while processing agent call"},
                                          epochSnapshot());
            }
            refreshEpoch();
            finishCall(std::move(pending), std::move(result), completions);
            ++completed;
        }

        {
            std::lock_guard lock(mutex);
            setStatusFromWorkLocked();
        }
        pump_lock.unlock();
        for (const auto &[done, result] : completions)
            invokeCompletion(done, result);
    }

    std::optional<std::uint64_t> epochSnapshot() const {
        std::lock_guard lock(mutex);
        return current_epoch;
    }

    void listenerLoop(AgentListener listener, std::string expected_bridge_token,
                      std::string gui_token) noexcept {
        std::optional<AgentChannel> client;
        bool authenticated = false;
        std::uint64_t session = 0;
        TimePoint hello_deadline{};
        std::string client_name;
        std::string client_version;

        try {
            for (;;) {
                bool should_stop_accepting = false;
                bool requested_stop = false;
                {
                    std::lock_guard lock(mutex);
                    should_stop_accepting = stopping;
                    requested_stop = stop_requested;
                }

                if (!should_stop_accepting) {
                    const auto accept_timeout =
                        client ? std::chrono::milliseconds{1} : std::chrono::milliseconds{10};
                    auto accepted = listener.accept(accept_timeout);
                    if (accepted) {
                        if (client) {
                            const AgentJson busy =
                                rpcError(nullptr, "BUSY", "another agent is connected");
                            (void)writeAgentMessage(*accepted, busy);
                            accepted->close();
                        } else {
                            client.emplace(std::move(*accepted));
                            session = allocateSession();
                            hello_deadline =
                                SteadyClock::now() +
                                std::max(config.hello_timeout, std::chrono::milliseconds::zero());
                            authenticated = false;
                            client_name.clear();
                            client_version.clear();
                        }
                    }
                } else if (!requested_stop) {
                    std::this_thread::sleep_for(std::chrono::milliseconds{10});
                }

                if (client && client->isOpen()) {
                    bool shutting_down = false;
                    {
                        std::lock_guard lock(mutex);
                        shutting_down = stopping;
                    }
                    if (!shutting_down) {
                        if (!authenticated && SteadyClock::now() >= hello_deadline) {
                            closeSession(client, authenticated, session, client_name,
                                         client_version);
                            authenticated = false;
                            session = 0;
                        } else {
                            std::string line;
                            const AgentReadStatus read_status =
                                client->readLine(line, std::chrono::milliseconds{1});
                            if (read_status == AgentReadStatus::Closed ||
                                read_status == AgentReadStatus::Oversized ||
                                read_status == AgentReadStatus::Error) {
                                closeSession(client, authenticated, session, client_name,
                                             client_version);
                                authenticated = false;
                                session = 0;
                            } else if (read_status == AgentReadStatus::Line) {
                                if (!authenticated) {
                                    if (!handleHello(line, *client, session, expected_bridge_token,
                                                     gui_token, client_name, client_version)) {
                                        closeSession(client, authenticated, session, client_name,
                                                     client_version);
                                        authenticated = false;
                                        session = 0;
                                    } else {
                                        authenticated = true;
                                        setConnected(session, client_name, client_version);
                                    }
                                } else if (!handleCall(line, session, client_name, *client)) {
                                    closeSession(client, authenticated, session, client_name,
                                                 client_version);
                                    authenticated = false;
                                    session = 0;
                                }
                            } else if (read_status == AgentReadStatus::Timeout) {
                                std::this_thread::sleep_for(std::chrono::milliseconds{8});
                            }
                        }
                    }
                } else if (client) {
                    closeSession(client, authenticated, session, client_name, client_version);
                    authenticated = false;
                    session = 0;
                }

                if (!flushOutbox(client, authenticated, session)) {
                    closeSession(client, authenticated, session, client_name, client_version);
                    authenticated = false;
                    session = 0;
                }

                bool queue_empty = false;
                {
                    std::lock_guard lock(mutex);
                    queue_empty = outbox.empty();
                    requested_stop = stop_requested;
                }
                if (requested_stop && queue_empty)
                    break;
            }
        } catch (...) {
            if (client)
                client->close();
            std::lock_guard lock(mutex);
            if (!stopping) {
                is_running = false;
                current_status = {AgentServerState::Error, {}, {}, "agent listener failed"};
            }
        }

        if (client)
            closeSession(client, authenticated, session, client_name, client_version);
        listener.close();
    }

    std::uint64_t allocateSession() {
        std::lock_guard lock(mutex);
        active_session = ++next_session;
        return active_session;
    }

    bool handleHello(const std::string &line, AgentChannel &channel, std::uint64_t session,
                     const std::string &expected_bridge_token, const std::string &gui_token,
                     std::string &name, std::string &version) {
        OrderedJson request;
        try {
            request = OrderedJson::parse(line);
        } catch (...) {
            return false;
        }
        if (!request.is_object() || !request.contains("jsonrpc") ||
            !request["jsonrpc"].is_string() || request["jsonrpc"] != "2.0" ||
            !request.contains("id") || !validRpcId(request["id"]) || !request.contains("method") ||
            !request["method"].is_string() || request["method"] != "hello" ||
            !request.contains("params") || !request["params"].is_object())
            return false;

        const OrderedJson &params = request["params"];
        if (!params.contains("protocol") || !params["protocol"].is_string() ||
            params["protocol"] != std::string{kAgentWireProtocol} ||
            !params.contains("bridge_token") || !params["bridge_token"].is_string() ||
            !agentTokensEqual(params["bridge_token"].get<std::string>(), expected_bridge_token) ||
            !params.contains("catalog_version") || !params["catalog_version"].is_number_integer() ||
            !params.contains("client") || !params["client"].is_object() ||
            !params["client"].contains("name") || !params["client"]["name"].is_string() ||
            !params["client"].contains("version") || !params["client"]["version"].is_string())
            return false;

        const bool catalog_matches = params["catalog_version"] == kAgentCatalogVersion;
        name = params["client"]["name"].get<std::string>();
        version = params["client"]["version"].get<std::string>();
        const AgentJson id = request["id"];
        if (!catalog_matches) {
            const AgentJson mismatch =
                rpcError(id, "VERSION_MISMATCH", "agent catalog version mismatch");
            (void)writeAgentMessage(channel, mismatch);
            return false;
        }

        std::optional<std::uint64_t> epoch;
        {
            std::lock_guard lock(mutex);
            if (active_session != session)
                return false;
            epoch = current_epoch;
        }
        const AgentJson hello_result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result",
             {{"gui_token", gui_token},
              {"app_version", config.app_version},
              {"catalog_version", kAgentCatalogVersion},
              {"epoch", epoch ? AgentJson(*epoch) : AgentJson(nullptr)}}}};
        return writeAgentMessage(channel, hello_result);
    }

    bool handleCall(const std::string &line, std::uint64_t session, const std::string &client_name,
                    AgentChannel &channel) {
        OrderedJson request;
        try {
            request = OrderedJson::parse(line);
        } catch (...) {
            return false;
        }
        const AgentJson id =
            request.is_object() && request.contains("id") && validRpcId(request["id"])
                ? AgentJson(request["id"])
                : AgentJson(nullptr);
        if (!agentJsonDepthWithin(request)) {
            const AgentJson invalid =
                rpcError(id, "INVALID_ARGUMENT", "call request is nested too deeply");
            return writeAgentMessage(channel, invalid);
        }
        if (!request.is_object() || !request.contains("jsonrpc") ||
            !request["jsonrpc"].is_string() || request["jsonrpc"] != "2.0" ||
            !request.contains("id") || !validRpcId(request["id"]) || !request.contains("method") ||
            !request["method"].is_string() || request["method"] != "call" ||
            !request.contains("params") || !request["params"].is_object()) {
            const AgentJson invalid = rpcError(id, "INVALID_ARGUMENT", "invalid call request");
            return writeAgentMessage(channel, invalid);
        }

        const OrderedJson &params = request["params"];
        if (!params.contains("tool") || !params["tool"].is_string() ||
            params["tool"].get_ref<const std::string &>().empty() ||
            !params.contains("arguments") || !params["arguments"].is_object()) {
            const AgentJson invalid = rpcError(id, "INVALID_ARGUMENT", "invalid call request");
            return writeAgentMessage(channel, invalid);
        }

        AgentCall call;
        call.tool = params["tool"].get<std::string>();
        call.arguments = params["arguments"];
        call.client = client_name;
        enqueueNetworkCall(std::move(call), session, id);
        return true;
    }

    void setConnected(std::uint64_t session, const std::string &name, const std::string &version) {
        std::lock_guard lock(mutex);
        if (active_session != session || stopping)
            return;
        authenticated_session = session;
        current_status.client_name = name;
        current_status.client_version = version;
        current_status.error.clear();
        setStatusFromWorkLocked();
    }

    void closeSession(std::optional<AgentChannel> &client, bool was_authenticated,
                      std::uint64_t session, std::string &name, std::string &version) {
        if (client) {
            client->close();
            client.reset();
        }
        {
            std::lock_guard lock(mutex);
            if (active_session == session)
                active_session = 0;
            if (was_authenticated && authenticated_session == session) {
                authenticated_session = 0;
                current_status.client_name.clear();
                current_status.client_version.clear();
                setStatusFromWorkLocked();
            }
            outbox.erase(std::remove_if(outbox.begin(), outbox.end(),
                                        [session](const OutgoingResponse &response) {
                                            return response.session == session;
                                        }),
                         outbox.end());
        }
        name.clear();
        version.clear();
    }

    bool flushOutbox(std::optional<AgentChannel> &client, bool authenticated,
                     std::uint64_t session) {
        for (;;) {
            OutgoingResponse response;
            bool have_response = false;
            {
                std::lock_guard lock(mutex);
                if (outbox.empty())
                    return true;
                if (!client || !authenticated || outbox.front().session != session) {
                    outbox.pop_front();
                    continue;
                }
                response = std::move(outbox.front());
                outbox.pop_front();
                have_response = true;
            }
            if (response.close_connection ||
                (have_response && !writeAgentMessage(*client, response.message)))
                return false;
        }
    }
};

AgentServer::AgentServer(IAgentCallExecutor &executor, IAgentHost &host, AgentServerConfig config)
    : m_config(std::move(config)), m_impl(std::make_unique<Impl>(executor, host, m_config)) {}

AgentServer::~AgentServer() { stop(); }

bool AgentServer::start(std::string *error) { return m_impl->start(error); }

void AgentServer::stop() { m_impl->stop(); }

bool AgentServer::running() const { return m_impl->running(); }

void AgentServer::submit(AgentCall call, Completion done) {
    m_impl->submit(std::move(call), std::move(done));
}

void AgentServer::pump() { m_impl->pump(); }

AgentServerStatus AgentServer::status() const { return m_impl->status(); }

const AgentServerConfig &AgentServer::config() const { return m_config; }
