// Issue #194 measurement harness. Measurement only; not a ctest target.
//   perf_app <scenario> [frames|path] [hide]
// Every run prints a `startup` line: the time to construct RfSimulatorApp.
//
// Frame scenarios mirror RfSimulatorCore::MainLoop (core/src/core.cpp): PollEvents,
// NewFrame, app work, Render, RenderDrawData, SwapBuffers. The window is hidden and
// vsync is off so the timings are CPU work, not refresh waits. Window flags are
// restored before teardown, because teardown saves them to app.ini.
//   default     default layout and default two-component graph
//   chain       default + generator -> attenuator -> amplifier, probe on amp out
//   analysis    chain + Network Analyzer and Receiver Requirements windows open
//   raw_in      40 raw ImGui::InputDouble fields per frame, in range
//   bounded_in  40 utils::inputDouble fields per frame, in range
//   bounded_out 40 utils::inputDouble fields per frame, all out of range
//   pump        AgentServer::pump() with the server off              (RFSIM_PERF_AGENT)
//   agent_calls per-tool testAgentCall durations on the chain         (RFSIM_PERF_AGENT)
// Project scenarios (no window loop):
//   save <path>  build the chain and write it as a project file
//   load <path>  load a project file 10 times and time each load
// hide: comma list of windows to hide for attribution: node,props,log,spectrum
#define IMGUI_DEFINE_MATH_OPERATORS
#include "amplifier_engine.h"
#include "app.h"
#include "attenuator_engine.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
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
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double microseconds(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double percentile95(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, v.size() * 95 / 100)];
}

double mean(const std::vector<double> &v) {
    double sum = 0.0;
    for (double x : v)
        sum += x;
    return sum / static_cast<double>(v.size());
}

struct Pins {
    int gen_out = -1; // pin ids for the window-loop NA settings
    int amp_out = -1;
    int gen_id = -1; // engine component ids: what the agent API addresses
    int amp_id = -1;
    bool ok = false;
};

// generator -> attenuator -> amplifier with a spectrum probe on the amplifier output.
Pins buildChain(RfSimulatorApp &app, int stages = 1) {
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
    pins.ok = true;
    return pins;
}

void printFrameResult(const std::string &scenario, const std::vector<double> &app_us,
                      const std::vector<double> &dsp_us, const std::vector<double> &draw_us,
                      const std::vector<double> &render_us) {
    std::printf("RESULT scenario=%s frames=%zu app_us_median=%.1f app_us_p95=%.1f "
                "app_us_mean=%.1f dsp_us_median=%.1f draw_us_median=%.1f "
                "render_us_median=%.1f render_us_mean=%.1f\n",
                scenario.c_str(), app_us.size(), median(app_us), percentile95(app_us), mean(app_us),
                median(dsp_us), median(draw_us), median(render_us), mean(render_us));
}

#ifdef RFSIM_PERF_AGENT
int runPump(RfSimulatorApp &app) {
    AgentServer *server = app.testAgentServer();
    if (!server)
        return 2;
    constexpr int kCalls = 200000;
    const auto t0 = Clock::now();
    for (int i = 0; i < kCalls; ++i)
        server->pump();
    const auto t1 = Clock::now();
    std::printf("RESULT scenario=pump calls=%d ns_per_call=%.1f\n", kCalls,
                microseconds(t1 - t0) * 1000.0 / kCalls);
    return 0;
}

std::string errorText(const AgentToolResult &r) {
    if (!r.is_error || !r.structured.contains("error"))
        return "";
    const auto &e = r.structured["error"];
    return e.value("code", std::string("?")) + ": " + e.value("message", std::string("?"));
}

// Times testAgentCall, the synchronous path the server's pump() runs on the UI thread
// (AgentApi::execute). Socket and framing costs are excluded. Results count only when
// is_error is false, so a rejected call is never reported as a valid timing.
int runAgentCalls(RfSimulatorApp &app, const Pins &pins) {
    nlohmann::ordered_json circuit_get = nlohmann::ordered_json::object();
    circuit_get["include_params"] = true;
    const AgentToolResult circuit = app.testAgentCall("circuit_get", circuit_get);
    if (circuit.is_error) {
        std::printf("RESULT scenario=agent_calls circuit_get_error=%s\n",
                    errorText(circuit).c_str());
        return 2;
    }
    const int epoch = circuit.structured.value("epoch", -1);
    int found = 0;
    for (const auto &c : circuit.structured.value("components", nlohmann::ordered_json::array())) {
        const int id = c.value("id", -1);
        found += (id == pins.gen_id || id == pins.amp_id) ? 1 : 0;
    }
    std::printf("RESULT scenario=agent_calls epoch=%d chain_ids_in_circuit_get=%d\n", epoch, found);
    if (found != 2)
        return 2;

    const auto timed = [&app](const char *tool, const nlohmann::ordered_json &args, int reps,
                              const char *case_name) {
        std::vector<double> us;
        AgentToolResult last;
        for (int i = 0; i < reps; ++i) {
            const auto t0 = Clock::now();
            last = app.testAgentCall(tool, args);
            us.push_back(microseconds(Clock::now() - t0));
        }
        std::printf("RESULT scenario=agent_calls tool=%s case=%s reps=%d is_error=%d "
                    "median_us=%.1f max_us=%.1f %s\n",
                    tool, case_name, reps, last.is_error ? 1 : 0, median(us),
                    *std::max_element(us.begin(), us.end()), errorText(last).c_str());
    };

    nlohmann::ordered_json at_gen = nlohmann::ordered_json::object();
    at_gen["component"] = pins.gen_id;
    at_gen["port"] = 0;
    nlohmann::ordered_json at_amp = nlohmann::ordered_json::object();
    at_amp["component"] = pins.amp_id;
    at_amp["port"] = 0;

    nlohmann::ordered_json component_get = nlohmann::ordered_json::object();
    component_get["epoch"] = epoch;
    component_get["component"] = pins.amp_id;
    timed("component_get", component_get, 5, "amp");

    nlohmann::ordered_json measure_port = nlohmann::ordered_json::object();
    measure_port["epoch"] = epoch;
    measure_port["at"] = at_amp;
    measure_port["max_tones"] = 8;
    timed("measure_port", measure_port, 5, "amp");

    for (int points : {201, 401, 2001}) {
        nlohmann::ordered_json na = nlohmann::ordered_json::object();
        na["epoch"] = epoch;
        na["point_a"] = at_gen;
        na["point_b"] = at_amp;
        na["start_Hz"] = 1.0e6;
        na["stop_Hz"] = 1.0e9;
        na["points"] = points;
        na["arrays"] = nlohmann::ordered_json{{"max_points", 101}};
        timed("network_analyzer_sweep", na, 3,
              points == 201   ? "201pts"
              : points == 401 ? "401pts"
                              : "2001pts");
    }

    // receiver_measure advances up to 4 ms per call; the UI stalls for one call at a time.
    // The loop is capped at 500 calls (about 2 s) so a stuck in_progress cannot hang the run.
    nlohmann::ordered_json rx = nlohmann::ordered_json::object();
    rx["epoch"] = epoch;
    rx["point_a"] = at_gen;
    rx["point_b"] = at_amp;
    rx["start_Hz"] = 1.0e9;
    rx["stop_Hz"] = 2.0e9;
    rx["points"] = 201;
    rx["output_power"] = true;
    std::vector<double> rx_us;
    bool in_progress = true;
    bool rx_error = false;
    std::string rx_error_text;
    const auto rx_t0 = Clock::now();
    for (int calls = 0; in_progress && calls < 500; ++calls) {
        const auto t0 = Clock::now();
        const AgentToolResult r = app.testAgentCall("receiver_measure", rx);
        rx_us.push_back(microseconds(Clock::now() - t0));
        rx_error = r.is_error;
        rx_error_text = errorText(r);
        in_progress = !r.is_error && r.structured.value("in_progress", false);
    }
    const double rx_total_ms = microseconds(Clock::now() - rx_t0) / 1000.0;
    std::printf("RESULT scenario=agent_calls tool=receiver_measure case=201pts calls=%zu "
                "still_in_progress=%d last_is_error=%d total_ms=%.1f median_call_us=%.1f "
                "max_call_us=%.1f %s\n",
                rx_us.size(), in_progress ? 1 : 0, rx_error ? 1 : 0, rx_total_ms, median(rx_us),
                *std::max_element(rx_us.begin(), rx_us.end()), rx_error_text.c_str());

    // test_flow_run sweeps tones[0].power_dBm on the generator and measures the amp output.
    for (int rows : {10, 50, 200, 1000}) {
        nlohmann::ordered_json values = nlohmann::ordered_json::array();
        for (int i = 0; i < rows; ++i)
            values.push_back(-40.0 + 30.0 * i / std::max(1, rows - 1));
        nlohmann::ordered_json cond = nlohmann::ordered_json::object();
        cond["component"] = pins.gen_id;
        cond["path"] = "tones[0].power_dBm";
        cond["values"] = values;
        nlohmann::ordered_json measure = nlohmann::ordered_json::object();
        measure["component"] = pins.amp_id;
        measure["port"] = 0;
        measure["metric"] = "power_dBm";
        nlohmann::ordered_json flow = nlohmann::ordered_json::object();
        flow["epoch"] = epoch;
        flow["conditions"] = nlohmann::ordered_json::array({cond});
        flow["measure"] = nlohmann::ordered_json::array({measure});
        const auto t0 = Clock::now();
        const AgentToolResult r = app.testAgentCall("test_flow_run", flow);
        const double ms = microseconds(Clock::now() - t0) / 1000.0;
        std::printf("RESULT scenario=agent_calls tool=test_flow_run case=%drows is_error=%d "
                    "total_ms=%.1f row_count=%d %s\n",
                    rows, r.is_error ? 1 : 0, ms, r.structured.value("row_count", -1),
                    errorText(r).c_str());
    }
    return 0;
}
#endif

int saveChain(RfSimulatorApp &app, const std::string &path) {
    const Pins pins = buildChain(app);
    if (!pins.ok)
        return 2;
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

int runFrames(RfSimulatorApp &app, GLFWwindow *window, const std::string &scenario, int frames) {
    constexpr int kWarmup = 60;
    constexpr int kFields = 40;
    const bool bounded = scenario.rfind("bounded", 0) == 0;
    const bool fields = bounded || scenario == "raw_in";
    double values[kFields];
    for (double &v : values)
        v = scenario == "bounded_out" ? 500.0 : 50.0;

    std::vector<double> app_us;
    std::vector<double> dsp_us;
    std::vector<double> draw_us;
    std::vector<double> render_us;
    std::vector<double> field_block_us;
    ImGuiIO &io = ImGui::GetIO();

    for (int frame = 0; frame < kWarmup + frames; ++frame) {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const auto t0 = Clock::now();
        app.update_dsp();
        const auto t1 = Clock::now();
        app.draw_ui();
        const auto t2 = Clock::now();

        if (fields) {
            ImGui::Begin("Bench fields");
            const auto f0 = Clock::now();
            for (int i = 0; i < kFields; ++i) {
                const std::string label = "field" + std::to_string(i) + "##bench";
                if (scenario == "raw_in")
                    ImGui::InputDouble(label.c_str(), &values[i], 0.1, 1.0, "%.3f");
                else
                    utils::inputDouble(label, values[i], 0.1, 1.0, "%.3f", 0.0, 100.0);
            }
            const auto f1 = Clock::now();
            ImGui::End();
            if (frame >= kWarmup)
                field_block_us.push_back(microseconds(f1 - f0));
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
            app_us.push_back(microseconds(t2 - t0));
            dsp_us.push_back(microseconds(t1 - t0));
            draw_us.push_back(microseconds(t2 - t1));
            render_us.push_back(microseconds(r1 - r0));
        }
    }

    printFrameResult(scenario, app_us, dsp_us, draw_us, render_us);
    if (fields) {
        const double block = median(field_block_us);
        std::printf("RESULT scenario=%s fields=%d field_block_us_median=%.1f "
                    "per_field_us_median=%.3f\n",
                    scenario.c_str(), kFields, block, block / kFields);
    }
    return 0;
}

int run(RfSimulatorApp &app, GLFWwindow *window, const std::string &scenario,
        const std::string &arg, const std::string &hide) {
    const auto hidden = [&hide](const char *name) { return hide.find(name) != std::string::npos; };
    if (scenario == "save")
        return saveChain(app, arg);
    if (scenario == "load")
        return loadProjectTimed(app, arg);

    Pins pins;
    if (scenario == "chain" || scenario == "analysis" || scenario == "agent_calls" ||
        scenario == "agent_calls10") {
        pins = buildChain(app, scenario == "agent_calls10" ? 8 : 1);
        if (!pins.ok) {
            std::fprintf(stderr, "chain setup failed\n");
            return 2;
        }
    }
    if (scenario == "analysis") {
        app.m_show_na = true;
        app.m_show_receiver_requirements = true;
        app.testNetworkAnalyzerEngine().setPointA(pins.gen_out);
        app.testNetworkAnalyzerEngine().setPointB(pins.amp_out);
    }
    if (hidden("node"))
        app.m_show_node_editor = false;
    if (hidden("props"))
        app.m_show_properties = false;
    if (hidden("log"))
        app.m_show_log = false;
    if (hidden("spectrum"))
        app.m_show_spectrum = false;
#ifdef RFSIM_PERF_AGENT
    if (scenario == "pump")
        return runPump(app);
    if (scenario == "agent_calls" || scenario == "agent_calls10")
        return runAgentCalls(app, pins);
#endif
    const int frames = std::atoi(arg.c_str());
    return runFrames(app, window, scenario, frames > 0 ? frames : 600);
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
        // The teardown saves these window flags to app.ini. Restore what the app loaded so
        // no scenario's hide, analysis, or project-load flags persist into later runs.
        const bool init_log = app.m_show_log;
        const bool init_spectrum = app.m_show_spectrum;
        const bool init_props = app.m_show_properties;
        const bool init_node = app.m_show_node_editor;
        const bool init_na = app.m_show_na;
        const bool init_receiver = app.m_show_receiver_requirements;
        status = run(app, window, scenario, arg, hide);
        app.m_show_log = init_log;
        app.m_show_spectrum = init_spectrum;
        app.m_show_properties = init_props;
        app.m_show_node_editor = init_node;
        app.m_show_na = init_na;
        app.m_show_receiver_requirements = init_receiver;
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
