#include "mcp_bridge.h"

#include "agent_endpoint.h"
#include "agent_wire.h"
#include "logging_core.h"
#include "mcp_bridge_detail.h"
#include "mcp_session.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {
using Json = nlohmann::json;
std::string escapeLogField(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character < 0x20 || character == 0x7f) {
                escaped += "\\x";
                escaped += hex[character >> 4];
                escaped += hex[character & 0x0f];
            } else {
                escaped.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return escaped;
}

struct BridgeEvent {
    enum class Kind { Line, Oversized, ReaderDone, ToolCallDone, CallerDone } kind;
    std::string line;
    std::uint64_t ticket = 0;
    McpToolCall call;
    AgentToolResult result;
    bool input_failed = false;
};

struct CallTask {
    std::uint64_t ticket;
    McpToolCall call;
    std::pair<std::string, std::string> client_info;
};

struct PendingCall {
    Json id;
    bool cancelled = false;
};

std::filesystem::path endpointFile(const BridgeOptions &options) {
    if (options.endpoint_file)
        return *options.endpoint_file;

    try {
        std::string error;
        const auto directory = defaultAgentEndpointDirectory(&error);
        if (!directory)
            return {};
        return *directory / agentEndpointFileName(agentInstallKey(options.exe_dir));
    } catch (const std::exception &) {
        return {};
    }
}

std::optional<std::string> successfulProtocol(const std::string &line, const McpStep &step) {
    Json request;
    try {
        request = Json::parse(line);
    } catch (const Json::exception &) {
        return std::nullopt;
    }
    if (!request.is_object() || !request.contains("method") || !request["method"].is_string())
        return std::nullopt;

    if (request["method"] == "initialize") {
        for (const auto &reply : step.replies) {
            if (reply.contains("result") && reply["result"].is_object() &&
                reply["result"].contains("protocolVersion") &&
                reply["result"]["protocolVersion"].is_string()) {
                return reply["result"]["protocolVersion"].get<std::string>();
            }
        }
        return std::nullopt;
    }

    if (!request.contains("params") || !request["params"].is_object() ||
        !request["params"].contains("_meta") || !request["params"]["_meta"].is_object())
        return std::nullopt;
    const auto &meta = request["params"]["_meta"];
    constexpr const char *protocol_version_key = "io.modelcontextprotocol/protocolVersion";
    if (!meta.contains(protocol_version_key) || !meta[protocol_version_key].is_string() ||
        meta[protocol_version_key] != "2026-07-28")
        return std::nullopt;

    const bool successful_reply = [&] {
        for (const auto &reply : step.replies) {
            if (reply.contains("result"))
                return true;
        }
        return false;
    }();
    if (successful_reply || step.call)
        return meta[protocol_version_key].get<std::string>();
    return std::nullopt;
}

} // namespace

int runBridge(std::istream &in, std::ostream &out, std::ostream &err,
              const BridgeOptions &options) {
    const auto endpoint_file = endpointFile(options);
    McpServerIdentity identity;
    identity.version = options.server_version;

    std::mutex event_mutex;
    std::condition_variable event_ready;
    std::deque<BridgeEvent> events;
    const auto post_event = [&](BridgeEvent event) {
        {
            std::lock_guard lock(event_mutex);
            events.push_back(std::move(event));
        }
        event_ready.notify_one();
    };

    std::mutex call_mutex;
    std::condition_variable call_ready;
    std::deque<CallTask> calls;
    bool finish_calls = false;

    std::mutex writer_mutex;
    bool output_failed = false;
    const auto write_reply = [&](const Json &reply) {
        std::lock_guard lock(writer_mutex);
        try {
            out << reply.dump() << '\n';
            out.flush();
            if (!out)
                output_failed = true;
        } catch (const std::exception &) {
            output_failed = true;
        }
    };
    std::mutex log_mutex;
    const auto write_log_line = [&](std::string_view line) {
        std::lock_guard lock(log_mutex);
        err << line << '\n';
        err.flush();
    };
    std::atomic<int> bridge_status{0};

    std::thread session_thread([&] {
        McpSession session(identity);
        std::map<std::uint64_t, PendingCall> pending_calls;
        std::uint64_t next_ticket = 1;
        bool reader_done = false;
        bool caller_done = false;
        bool client_logged = false;
        int status = 0;

        while (!reader_done || !caller_done) {
            BridgeEvent event{BridgeEvent::Kind::Line};
            {
                std::unique_lock lock(event_mutex);
                event_ready.wait(lock, [&] { return !events.empty(); });
                event = std::move(events.front());
                events.pop_front();
            }

            switch (event.kind) {
            case BridgeEvent::Kind::Line: {
                McpStep step = session.onLine(event.line);
                if (!client_logged) {
                    if (const auto protocol = successfulProtocol(event.line, step)) {
                        const auto client = session.clientInfo();
                        const std::string record = "rf-sim-mcp: " + escapeLogField(client.first) +
                                                   " " + escapeLogField(client.second) +
                                                   ", protocol " + *protocol;
                        write_log_line(record);
                        client_logged = true;
                    }
                }
                for (const auto &reply : step.replies)
                    write_reply(reply);
                if (step.cancel_id) {
                    for (auto &[ticket, pending] : pending_calls) {
                        (void)ticket;
                        if (pending.id == *step.cancel_id)
                            pending.cancelled = true;
                    }
                }
                if (step.call) {
                    const std::uint64_t ticket = next_ticket++;
                    pending_calls.emplace(ticket, PendingCall{step.call->id, false});
                    CallTask task{ticket, std::move(*step.call), session.clientInfo()};
                    {
                        std::lock_guard lock(call_mutex);
                        calls.push_back(std::move(task));
                    }
                    call_ready.notify_one();
                }
                break;
            }
            case BridgeEvent::Kind::Oversized: {
                const McpStep step = session.onOversizedLine();
                for (const auto &reply : step.replies)
                    write_reply(reply);
                break;
            }
            case BridgeEvent::Kind::ReaderDone: {
                reader_done = true;
                if (event.input_failed)
                    status = 1;
                {
                    std::lock_guard lock(call_mutex);
                    finish_calls = true;
                }
                call_ready.notify_one();
                break;
            }
            case BridgeEvent::Kind::ToolCallDone: {
                const auto pending = pending_calls.find(event.ticket);
                if (pending != pending_calls.end()) {
                    if (!pending->second.cancelled)
                        write_reply(session.toolCallResponse(event.call, event.result));
                    pending_calls.erase(pending);
                }
                break;
            }
            case BridgeEvent::Kind::CallerDone:
                caller_done = true;
                break;
            }
        }

        if (output_failed)
            status = 1;
        bridge_status.store(status);
    });

    std::thread caller_thread([&] {
        std::unique_ptr<GuiLink> link;
        std::optional<std::pair<std::string, std::string>> link_client;
        for (;;) {
            CallTask task;
            {
                std::unique_lock lock(call_mutex);
                call_ready.wait(lock, [&] { return finish_calls || !calls.empty(); });
                if (calls.empty() && finish_calls)
                    break;
                task = std::move(calls.front());
                calls.pop_front();
            }

            AgentToolResult result;
            try {
                if (!link || !link_client || *link_client != task.client_info) {
                    link.reset();
                    link = std::make_unique<GuiLink>(endpoint_file, options.timeouts);
                    link->setClientInfo(task.client_info.first, task.client_info.second);
                    link_client = task.client_info;
                }
                result = options.call_override
                             ? options.call_override(task.call.tool, task.call.arguments)
                             : link->call(task.call.tool, task.call.arguments);
                if (!options.call_override && link) {
                    const std::string cause = link->lastConnectError();
                    if (!cause.empty())
                        write_log_line("rf-sim-mcp: RF Simulator unreachable: " +
                                       escapeLogField(cause));
                }
            } catch (const std::exception &error) {
                LOG_ERROR("MCP bridge call for tool %s failed: %s", task.call.tool.c_str(),
                          error.what());
                write_log_line("rf-sim-mcp: bridge call exception: " +
                               escapeLogField(error.what()));
                result = mcp_bridge_detail::internalFailureResult();
            } catch (...) {
                LOG_ERROR("MCP bridge call for tool %s failed with unknown exception",
                          task.call.tool.c_str());
                write_log_line("rf-sim-mcp: bridge call failed with an unknown exception");
                result = mcp_bridge_detail::internalFailureResult();
            }

            BridgeEvent completed{BridgeEvent::Kind::ToolCallDone};
            completed.ticket = task.ticket;
            completed.call = std::move(task.call);
            completed.result = std::move(result);
            post_event(std::move(completed));
        }
        post_event(BridgeEvent{BridgeEvent::Kind::CallerDone});
    });

    std::thread reader_thread([&] {
        AgentLineReader line_reader;
        bool input_failed = false;
        std::string line;
        bool discarding_oversized = false;
        std::size_t line_bytes = 0;
        const auto queue_frame = [&](AgentLineReader::Next next) {
            if (next == AgentLineReader::Next::Line) {
                BridgeEvent event{BridgeEvent::Kind::Line};
                event.line = std::move(line);
                post_event(std::move(event));
            } else if (next == AgentLineReader::Next::Oversized) {
                post_event(BridgeEvent{BridgeEvent::Kind::Oversized});
            }
        };
        try {
            char byte = '\0';
            while (in.get(byte)) {
                if (discarding_oversized) {
                    if (byte == '\n') {
                        line_reader.append(std::string_view(&byte, 1));
                        (void)line_reader.next(line);
                        discarding_oversized = false;
                        line_bytes = 0;
                    }
                    continue;
                }

                line_reader.append(std::string_view(&byte, 1));
                if (byte == '\n') {
                    for (;;) {
                        const auto next = line_reader.next(line);
                        if (next == AgentLineReader::Next::NeedMore)
                            break;
                        queue_frame(next);
                    }
                    line_bytes = 0;
                    continue;
                }

                ++line_bytes;
                const bool definitely_oversized =
                    line_bytes > kAgentMaxLineBytes &&
                    (byte != '\r' || line_bytes > kAgentMaxLineBytes + 1);
                if (definitely_oversized) {
                    const auto next = line_reader.next(line);
                    queue_frame(next);
                    discarding_oversized = next == AgentLineReader::Next::Oversized;
                    line_bytes = 0;
                }
            }
            input_failed = in.bad() || (!in.eof() && in.fail());
        } catch (...) {
            input_failed = true;
        }
        BridgeEvent done{BridgeEvent::Kind::ReaderDone};
        done.input_failed = input_failed;
        post_event(std::move(done));
    });

    session_thread.join();
    reader_thread.join();
    caller_thread.join();
    return bridge_status.load();
}
