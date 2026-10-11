// Issue #194 measurement harness. Measurement only; not a ctest target.
//   build/bin/perf_app.exe <scenario> [frames] [hide]
// Build: cmake --build build --target perf_app. RFSIM_PERF_AGENT is set only in the v0.28.0 tree.
//
// The frame loop mirrors RfSimulatorCore::MainLoop (core/src/core.cpp): PollEvents, NewFrame,
// the default dock layout (frame 0 only, the core first-run block), DockSpaceOverViewport, app work
// (update_dsp, then draw_ui), Render, RenderDrawData, SwapBuffers. The window is hidden and vsync
// is off, so the timings are CPU work. Each frame scenario runs 60 warm-up frames, then `frames`.
//
// Frame scenarios:
//   default     the project as loaded
//   chain       generator -> attenuator -> amplifier, spectrum probe on the amplifier output
//   analysis    chain; Network Analyzer window shown, points A/B set, sweep 50-150 MHz, 201 points
//   receiver    analysis plus a receiver requirement with output-power limits and a 100 MHz
//               reference tone, so the receiver measurement runs
//   drag        chain; the generator tone power changes every frame and the project is marked
//               dirty. Engine-side proxy for a slider drag: no mouse input is simulated.
//   nodes50     chain plus 50 unconnected attenuators
//   raw_in      40 ImGui::InputDouble fields per frame, in range
//   bounded_in  40 utils::inputDouble fields per frame, in range
//   bounded_out 40 utils::inputDouble fields per frame, each reset to an out-of-range value
// Agent scenarios (RFSIM_PERF_AGENT only): pump, agent_calls (1 stage), agent_calls10 (8 stages).
// Project scenarios: save <path> writes the chain; load <path> loads a project 10 times.
// hide: comma list from node,props,log,spectrum; hides that window for attribution.
//
// Window state: teardown saves window flags to app.ini next to the executable. The harness sets the
// baseline flags before and after each scenario, so no scenario's flags persist into later runs.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "amplifier_engine.h"
#include "app.h"
#include "attenuator_engine.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "imgui_internal.h"
#include "network_analyzer_engine.h"
#include "receiver_performance_measurement.h"
#include "receiver_requirements.h"
#include "signal_generator_engine.h"
#include "utils.h"
#include <GLFW/glfw3.h>
#include <imnodes.h>
#include <implot.h>

#ifdef RFSIM_PERF_AGENT
#include "agent_server.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::ordered_json;

double microseconds(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

// Every caller passes a non-empty vector.
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double percentile95(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, v.size() * 95 / 100)];
}

struct Pins {
    int gen_out = -1; // output pin ids, used by the Network Analyzer window
    int amp_out = -1;
    int gen_id = -1; // component ids, used by the agent API
    int amp_id = -1;
    SignalGeneratorEngine *gen = nullptr;
    bool ok = false;
};

// generator -> `stages` attenuators -> amplifier, spectrum probe on the amplifier output.
Pins buildChain(RfSimulatorApp &app, int stages) {
    Pins pins;
    auto *gen = dynamic_cast<SignalGeneratorEngine *>(app.testCreateComponent("generator", 9201));
    auto *amp = dynamic_cast<AmplifierEngine *>(app.testCreateComponent("amplifier", 9203));
    if (!gen || !amp)
        return pins;
    gen->addTone(100e6, -20.0);
    int upstream_pin = gen->outputPinId();
    for (int i = 0; i < stages; ++i) {
        auto *att =
            dynamic_cast<AttenuatorEngine *>(app.testCreateComponent("attenuator", 9202 + 100 * i));
        if (!att || !app.testConnectLink(upstream_pin, att->inputPinId()))
            return pins;
        upstream_pin = att->outputPinId();
    }
    if (!app.testConnectLink(upstream_pin, amp->inputPinId()) ||
        !app.testAddProbePin(amp->outputPinId()))
        return pins;
    pins.gen_out = gen->outputPinId();
    pins.amp_out = amp->outputPinId();
    pins.gen_id = gen->id();
    pins.amp_id = amp->id();
    pins.gen = gen;
    pins.ok = true;
    return pins;
}

void setBaselineWindows(RfSimulatorApp &app) {
    app.m_show_log = true;
    app.m_show_spectrum = true;
    app.m_show_properties = true;
    app.m_show_node_editor = true;
    app.m_show_na = false;
    app.m_show_receiver_requirements = false;
}

// The first-run dock layout from RfSimulatorCore::MainLoop, applied once at frame 0.
void buildDefaultDockLayout() {
    const ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

    ImGuiID dock_root = dockspace_id;
    const ImGuiID dock_bottom =
        ImGui::DockBuilderSplitNode(dock_root, ImGuiDir_Down, 0.20f, nullptr, &dock_root);
    ImGuiID dock_right =
        ImGui::DockBuilderSplitNode(dock_root, ImGuiDir_Right, 0.30f, nullptr, &dock_root);
    const ImGuiID dock_properties =
        ImGui::DockBuilderSplitNode(dock_root, ImGuiDir_Down, 0.35f, nullptr, &dock_root);
    const ImGuiID dock_iq =
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.40f, nullptr, &dock_right);

    ImGui::DockBuilderDockWindow("Node Editor", dock_root);
    ImGui::DockBuilderDockWindow("Properties", dock_properties);
    ImGui::DockBuilderDockWindow("Spectrum Analyzer", dock_right);
    ImGui::DockBuilderDockWindow("IQ Plot", dock_iq);
    ImGui::DockBuilderDockWindow("Log", dock_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
}

void printFrameResult(const std::string &scenario, const std::vector<double> &frame_us,
                      const std::vector<double> &app_us, const std::vector<double> &dsp_us,
                      const std::vector<double> &draw_us, const std::vector<double> &render_us) {
    std::printf("RESULT scenario=%s frames=%zu frame_us_median=%.1f frame_us_p95=%.1f "
                "app_us_median=%.1f app_us_p95=%.1f dsp_us_median=%.1f draw_us_median=%.1f "
                "render_us_median=%.1f\n",
                scenario.c_str(), frame_us.size(), median(frame_us), percentile95(frame_us),
                median(app_us), percentile95(app_us), median(dsp_us), median(draw_us),
                median(render_us));
}

#ifdef RFSIM_PERF_AGENT
int runPump(RfSimulatorApp &app) {
    AgentServer *server = app.testAgentServer();
    if (!server) {
        std::printf("INVALID pump no_agent_server\n");
        return 2;
    }
    constexpr int kCalls = 200000;
    const auto t0 = Clock::now();
    for (int i = 0; i < kCalls; ++i)
        server->pump();
    const double ns_per_call = microseconds(Clock::now() - t0) * 1000.0 / kCalls;
    std::printf("RESULT scenario=pump calls=%d ns_per_call=%.1f\n", kCalls, ns_per_call);
    return 0;
}

std::string errorText(const AgentToolResult &r) {
    if (!r.is_error || !r.structured.contains("error"))
        return "";
    const Json &e = r.structured["error"];
    return e.value("code", std::string("?")) + ": " + e.value("message", std::string("?"));
}

struct CallTiming {
    bool ok = false;
    std::string error;
    int calls = 0;
    double first_us = 0.0;
    double warm_median_us = 0.0; // median of calls 2..n; meaningful only when calls > 1
    double max_us = 0.0;
    AgentToolResult last;
};

// Times AgentApi::execute through testAgentCall. This is the synchronous path that the server's
// pump() runs on the UI thread; socket and framing costs are excluded. A call whose result is an
// error marks the case INVALID, and its timing is never reported as valid.
CallTiming timeTool(RfSimulatorApp &app, const char *tool, const Json &args, int reps) {
    CallTiming timing;
    std::vector<double> us;
    for (int i = 0; i < reps; ++i) {
        const auto t0 = Clock::now();
        timing.last = app.testAgentCall(tool, args);
        us.push_back(microseconds(Clock::now() - t0));
        ++timing.calls;
        if (timing.last.is_error) {
            timing.error = errorText(timing.last);
            return timing;
        }
    }
    timing.ok = true;
    timing.first_us = us.front();
    timing.max_us = *std::max_element(us.begin(), us.end());
    if (us.size() > 1)
        timing.warm_median_us = median(std::vector<double>(us.begin() + 1, us.end()));
    return timing;
}

void printTool(const char *tool, const char *case_name, const CallTiming &t,
               const std::string &detail) {
    if (!t.ok) {
        std::printf("INVALID agent tool=%s case=%s error=%s\n", tool, case_name, t.error.c_str());
        return;
    }
    std::printf("RESULT agent tool=%s case=%s calls=%d first_us=%.1f max_us=%.1f", tool, case_name,
                t.calls, t.first_us, t.max_us);
    if (t.calls > 1)
        std::printf(" warm_median_us=%.1f", t.warm_median_us);
    std::printf(" %s\n", detail.c_str());
}

// Agent calls on the chain. Component ids come from the engines and the epoch comes from a fresh
// circuit_get. The fixture is checked against circuit_get before any call is timed.
int runAgentCalls(RfSimulatorApp &app, const Pins &pins, std::size_t expected_components) {
    Json circuit_args = Json::object();
    circuit_args["include_params"] = true;
    const CallTiming circuit = timeTool(app, "circuit_get", circuit_args, 5);
    if (!circuit.ok) {
        printTool("circuit_get", "chain", circuit, "");
        return 2;
    }
    const int epoch = circuit.last.structured.value("epoch", -1);
    const Json components = circuit.last.structured.value("components", Json::array());
    int gen_found = 0;
    int amp_found = 0;
    for (const Json &c : components) {
        gen_found += c.value("id", -1) == pins.gen_id ? 1 : 0;
        amp_found += c.value("id", -1) == pins.amp_id ? 1 : 0;
    }
    std::printf("FIXTURE agent components_app=%zu components_circuit_get=%zu gen_found=%d "
                "amp_found=%d epoch=%d\n",
                expected_components, components.size(), gen_found, amp_found, epoch);
    if (components.size() != expected_components || gen_found != 1 || amp_found != 1 || epoch < 0) {
        std::printf("INVALID agent fixture_mismatch\n");
        return 2;
    }
    printTool("circuit_get", "chain", circuit, "components=" + std::to_string(components.size()));

    Json at_gen = Json::object();
    at_gen["component"] = pins.gen_id;
    at_gen["port"] = 0;
    Json at_amp = Json::object();
    at_amp["component"] = pins.amp_id;
    at_amp["port"] = 0;

    const CallTiming types = timeTool(app, "component_types", Json::object(), 5);
    printTool("component_types", "all", types,
              types.ok ? "types=" + std::to_string(
                                        types.last.structured.value("types", Json::array()).size())
                       : "");

    Json library_args = Json::object();
    library_args["query"] = "AM1143";
    library_args["limit"] = 5;
    const CallTiming library = timeTool(app, "library_search", library_args, 5);
    printTool("library_search", "AM1143", library,
              library.ok ? "total=" + std::to_string(library.last.structured.value("total", -1))
                         : "");

    Json data_args = Json::object();
    data_args["part_number"] = "AM1143";
    const CallTiming data = timeTool(app, "data_file_read", data_args, 5);
    printTool("data_file_read", "AM1143", data,
              data.ok ? "points=" + std::to_string(data.last.structured.value("points", -1)) : "");

    Json component_args = Json::object();
    component_args["epoch"] = epoch;
    component_args["component"] = pins.amp_id;
    printTool("component_get", "amp", timeTool(app, "component_get", component_args, 5), "");

    Json port_args = Json::object();
    port_args["epoch"] = epoch;
    port_args["at"] = at_amp;
    port_args["max_tones"] = 8;
    const CallTiming port = timeTool(app, "measure_port", port_args, 5);
    printTool("measure_port", "amp", port,
              port.ok ? "tone_count=" + std::to_string(port.last.structured.value("tone_count", -1))
                      : "");

    // Each sweep point count is a new settings signature, so the first call is a cache miss.
    for (int points : {201, 401, 2001}) {
        Json na = Json::object();
        na["epoch"] = epoch;
        na["point_a"] = at_gen;
        na["point_b"] = at_amp;
        na["start_Hz"] = 50e6;
        na["stop_Hz"] = 150e6;
        na["points"] = points;
        na["arrays"] = Json{{"max_points", 101}};
        const CallTiming sweep = timeTool(app, "network_analyzer_sweep", na, 3);
        std::string detail;
        if (sweep.ok) {
            const Json summary = sweep.last.structured.value("summary", Json::object());
            detail = "valid_points=" + std::to_string(summary.value("valid_points", -1));
        }
        printTool("network_analyzer_sweep", (std::to_string(points) + "pts").c_str(), sweep,
                  detail);
    }

    // receiver_measure advances up to 4 ms per call and reports in_progress. The loop runs until
    // the sweep completes, capped at 500 calls, so a stuck in_progress flag cannot hang the run.
    Json rx = Json::object();
    rx["epoch"] = epoch;
    rx["point_a"] = at_gen;
    rx["point_b"] = at_amp;
    rx["start_Hz"] = 50e6;
    rx["stop_Hz"] = 150e6;
    rx["points"] = 201;
    rx["output_power"] = true;
    std::vector<double> rx_us;
    bool in_progress = true;
    bool rx_error = false;
    std::string rx_error_text;
    int output_non_null = 0;
    const auto rx_t0 = Clock::now();
    for (int calls = 0; in_progress && calls < 500; ++calls) {
        const auto t0 = Clock::now();
        const AgentToolResult r = app.testAgentCall("receiver_measure", rx);
        rx_us.push_back(microseconds(Clock::now() - t0));
        if (r.is_error) {
            rx_error = true;
            rx_error_text = errorText(r);
            break;
        }
        in_progress = r.structured.value("in_progress", false);
        output_non_null = 0;
        for (const Json &v : r.structured.value("output_power_dBm", Json::array()))
            output_non_null += v.is_number() ? 1 : 0;
    }
    const double rx_total_ms = microseconds(Clock::now() - rx_t0) / 1000.0;
    if (rx_error) {
        std::printf("INVALID agent tool=receiver_measure case=201pts error=%s\n",
                    rx_error_text.c_str());
    } else {
        std::printf("RESULT agent tool=receiver_measure case=201pts calls=%zu completed=%d "
                    "output_power_non_null=%d total_ms=%.2f median_call_us=%.1f max_call_us=%.1f\n",
                    rx_us.size(), in_progress ? 0 : 1, output_non_null, rx_total_ms, median(rx_us),
                    *std::max_element(rx_us.begin(), rx_us.end()));
    }

    // test_flow_run sweeps tones[0].power_dBm on the generator and measures the amplifier output.
    // The executor rejects sweeps above 1000 rows before running any row.
    for (int rows : {10, 50, 200, 1000}) {
        Json values = Json::array();
        for (int i = 0; i < rows; ++i)
            values.push_back(-40.0 + 30.0 * i / std::max(1, rows - 1));
        Json cond = Json::object();
        cond["component"] = pins.gen_id;
        cond["path"] = "tones[0].power_dBm";
        cond["values"] = values;
        Json measure = Json::object();
        measure["component"] = pins.amp_id;
        measure["port"] = 0;
        measure["metric"] = "power_dBm";
        Json flow = Json::object();
        flow["epoch"] = epoch;
        flow["conditions"] = Json::array({cond});
        flow["measure"] = Json::array({measure});
        const CallTiming run = timeTool(app, "test_flow_run", flow, 1);
        printTool("test_flow_run", (std::to_string(rows) + "rows").c_str(), run,
                  run.ok ? "row_count=" + std::to_string(run.last.structured.value("row_count", -1))
                         : "");
    }

    // One add. Removal is not timed, because it needs an extra ref-based call.
    Json add_op = Json::object();
    add_op["op"] = "add";
    add_op["type"] = "attenuator";
    add_op["ref"] = "tmp1";
    Json add_args = Json::object();
    add_args["epoch"] = epoch;
    add_args["ops"] = Json::array({add_op});
    printTool("circuit_edit", "add", timeTool(app, "circuit_edit", add_args, 1), "");
    return 0;
}
#endif

int saveChain(RfSimulatorApp &app, const std::string &path) {
    const Pins pins = buildChain(app, 1);
    if (!pins.ok) {
        std::printf("INVALID save chain_setup_failed\n");
        return 2;
    }
    app.saveProject(path);
    std::printf("RESULT scenario=save path_saved=1\n");
    return 0;
}

int loadProjectTimed(RfSimulatorApp &app, const std::string &path) {
    std::vector<double> ms;
    for (int i = 0; i < 10; ++i) {
        const auto t0 = Clock::now();
        app.loadProject(path);
        ms.push_back(microseconds(Clock::now() - t0) / 1000.0);
    }
    std::printf("RESULT scenario=load components=%zu load_ms_first=%.2f load_ms_median=%.2f "
                "load_ms_max=%.2f\n",
                app.componentCount(), ms.front(), median(ms),
                *std::max_element(ms.begin(), ms.end()));
    return 0;
}

int runFrames(RfSimulatorApp &app, GLFWwindow *window, const std::string &scenario, int frames,
              const Pins &pins) {
    constexpr int kWarmup = 60;
    constexpr int kFields = 40;
    const bool fields =
        scenario == "raw_in" || scenario == "bounded_in" || scenario == "bounded_out";
    double values[kFields];
    for (double &v : values)
        v = 50.0;

    std::vector<double> frame_us;
    std::vector<double> app_us;
    std::vector<double> dsp_us;
    std::vector<double> draw_us;
    std::vector<double> render_us;
    std::vector<double> field_us;
    ImGuiIO &io = ImGui::GetIO();

    for (int frame = 0; frame < kWarmup + frames; ++frame) {
        const auto frame_t0 = Clock::now();
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (frame == 0)
            buildDefaultDockLayout();
        ImGui::DockSpaceOverViewport(ImGui::GetID("MainDockSpace"), nullptr,
                                     ImGuiDockNodeFlags_PassthruCentralNode);

        const auto t0 = Clock::now();
        if (scenario == "drag") {
            pins.gen->updateTone(0, 100e6, -20.0 + 3.0 * std::sin(0.2 * frame));
            app.testMakeDirty();
        }
        app.update_dsp();
        const auto t1 = Clock::now();
        app.draw_ui();
        const auto t2 = Clock::now();

        if (fields) {
            ImGui::Begin("Bench fields");
            const auto f0 = Clock::now();
            for (int i = 0; i < kFields; ++i) {
                if (scenario == "bounded_out")
                    values[i] = 500.0; // an external out-of-range write, every frame
                const std::string label = "field" + std::to_string(i) + "##bench";
                if (scenario == "raw_in")
                    ImGui::InputDouble(label.c_str(), &values[i], 0.1, 1.0, "%.3f");
                else
                    utils::inputDouble(label, values[i], 0.1, 1.0, "%.3f", 0.0, 100.0);
            }
            const auto f1 = Clock::now();
            ImGui::End();
            if (frame >= kWarmup)
                field_us.push_back(microseconds(f1 - f0));
        }

        const auto r0 = Clock::now();
        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(window);
        }
        const auto r1 = Clock::now();
        glfwSwapBuffers(window);

        if (frame >= kWarmup) {
            frame_us.push_back(microseconds(Clock::now() - frame_t0));
            app_us.push_back(microseconds(t2 - t0));
            dsp_us.push_back(microseconds(t1 - t0));
            draw_us.push_back(microseconds(t2 - t1));
            render_us.push_back(microseconds(r1 - r0));
        }
    }

    printFrameResult(scenario, frame_us, app_us, dsp_us, draw_us, render_us);
    if (fields) {
        const double block = median(field_us);
        std::printf("RESULT scenario=%s fields=%d field_block_us_median=%.1f "
                    "per_field_us_median=%.3f\n",
                    scenario.c_str(), kFields, block, block / kFields);
    }
    if (scenario == "receiver") {
        const auto &rx = app.testReceiverPerformanceMeasurementEngine();
        std::printf("FIXTURE scenario=receiver output_power_points=%zu in_progress=%d\n",
                    rx.measurements().output_power_dBm.size(), rx.isInProgress() ? 1 : 0);
    }
    return 0;
}

bool isFrameScenario(const std::string &s) {
    for (const char *name : {"default", "chain", "analysis", "receiver", "drag", "nodes50",
                             "raw_in", "bounded_in", "bounded_out"}) {
        if (s == name)
            return true;
    }
    return false;
}

int run(RfSimulatorApp &app, GLFWwindow *window, const std::string &scenario,
        const std::string &arg, const std::string &hide) {
    const auto hidden = [&hide](const char *name) { return hide.find(name) != std::string::npos; };
    if (scenario == "save")
        return saveChain(app, arg);
    if (scenario == "load")
        return loadProjectTimed(app, arg);

    const bool agent_fixture = scenario == "agent_calls" || scenario == "agent_calls10";
    if (scenario == "pump" || agent_fixture) {
#ifndef RFSIM_PERF_AGENT
        std::fprintf(stderr, "scenario %s needs RFSIM_PERF_AGENT\n", scenario.c_str());
        return 2;
#endif
    } else if (!isFrameScenario(scenario)) {
        std::fprintf(stderr, "unknown scenario %s\n", scenario.c_str());
        return 2;
    }

    const bool chain = scenario == "chain" || scenario == "analysis" || scenario == "receiver" ||
                       scenario == "drag" || scenario == "nodes50" || agent_fixture;
    Pins pins;
    if (chain) {
        pins = buildChain(app, scenario == "agent_calls10" ? 8 : 1);
        if (!pins.ok) {
            std::printf("INVALID %s chain_setup_failed\n", scenario.c_str());
            return 2;
        }
    }
    if (scenario == "nodes50") {
        for (int i = 0; i < 50; ++i)
            app.testCreateComponent("attenuator", 9500 + i);
    }
    if (scenario == "analysis" || scenario == "receiver") {
        NetworkAnalyzerEngine &na = app.testNetworkAnalyzerEngine();
        na.setStartFrequency(50e6);
        na.setStopFrequency(150e6);
        na.setPoints(201);
        na.setPointA(pins.gen_out);
        na.setPointB(pins.amp_out);
        app.m_show_na = true;
        app.m_show_receiver_requirements = true;
    }
    if (scenario == "receiver") {
        ReceiverRequirementsConfig config;
        config.band_start_Hz = 50e6;
        config.band_stop_Hz = 150e6;
        config.output_power = ReceiverOutputPowerLimits{-200.0, 200.0};
        config.measurement_conditions.output_reference_tone_frequency_Hz = 100e6;
        app.testReceiverRequirementsState().config = config;
        std::printf("FIXTURE scenario=receiver config_set=%d invalid_reason_empty=%d\n",
                    app.testReceiverRequirementsState().config.has_value() ? 1 : 0,
                    app.testReceiverRequirementsState().invalid_reason.empty() ? 1 : 0);
    }
    if (hidden("node"))
        app.m_show_node_editor = false;
    if (hidden("props"))
        app.m_show_properties = false;
    if (hidden("log"))
        app.m_show_log = false;
    if (hidden("spectrum"))
        app.m_show_spectrum = false;
    std::printf("FIXTURE scenario=%s components=%zu\n", scenario.c_str(), app.componentCount());

    if (scenario == "pump") {
#ifdef RFSIM_PERF_AGENT
        return runPump(app);
#endif
    }
    if (agent_fixture) {
#ifdef RFSIM_PERF_AGENT
        return runAgentCalls(app, pins, app.componentCount());
#endif
    }
    const int frames = std::atoi(arg.c_str());
    return runFrames(app, window, scenario, frames > 0 ? frames : 600, pins);
}

} // namespace

int main(int argc, char **argv) {
    const std::string scenario = argc > 1 ? argv[1] : "default";
    const std::string arg = argc > 2 ? argv[2] : "600";
    const std::string hide = argc > 3 ? argv[3] : "";

    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *window = glfwCreateWindow(1920, 1080, "RF Simulator perf", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImNodes::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad |
                      ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    int status = 0;
    {
        const auto construct0 = Clock::now();
        RfSimulatorApp app;
        std::printf("RESULT scenario=startup construct_ms=%.2f\n",
                    microseconds(Clock::now() - construct0) / 1000.0);
        setBaselineWindows(app);
        status = run(app, window, scenario, arg, hide);
        setBaselineWindows(app);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    ImNodes::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}
