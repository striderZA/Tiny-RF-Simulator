// Network Analyzer v3 — engine + widget tests (Task 3 rewrite).
//
// The v3 engine is a singleton instrument panel, not an IComponentEngine: it
// is constructed directly with (NodeGraphEngine&, IMeasurementChainHost&) and
// measures a chain of REAL graph components on a private, throwaway clone
// chain ("cheat" mode — the real simulation is never read for signal purposes
// and never written to; see network_analyzer_engine.h). These tests build
// small real DUT chains (plain engines + NodeGraphEngine links), inject a
// test-local IMeasurementChainHost, and verify runner and analyzer measurement semantics.
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "combiner_engine.h"
#include "common.h"
#include "measurement_chain_runner.h"
#include "mixer_engine.h"
#include "network_analyzer_engine.h"
#include "network_analyzer_widget.h"
#include "node_graph_engine.h"
#include "rf_switch_2to1_engine.h"
#include "rf_switch_engine.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include "splitter_engine.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <initializer_list>
#include <map>
#include <memory>
#include <string_view>
#include <tuple>
#include <vector>

using Catch::Matchers::WithinAbs;

namespace {

// Test-local implementation of the engine's injected lookups (see
// network_analyzer_engine.h). componentForNode resolves a real graph node to
// the live test engine; beginScratchPass hands out a throwaway scratch graph
// whose createClone builds a fresh engine of the requested type (params are
// applied afterwards via deserialize()). Hand-rolled here because the
// app-layer adapter (RfSimulatorApp::NaHost) is a private nested class that
// cannot be reused without dragging the whole app into this standalone target.
class TestNaScratch final : public IMeasurementChainScratch {
  public:
    IComponentEngine *createClone(std::string_view type, int id) override {
        if (type == "generator")
            return make<SignalGeneratorEngine>(id);
        if (type == "attenuator")
            return make<AttenuatorEngine>(id);
        if (type == "amplifier")
            return make<AmplifierEngine>(id);
        if (type == "mixer")
            return make<MixerEngine>(id);
        if (type == "splitter")
            return make<SplitterEngine>(id);
        if (type == "combiner")
            return make<CombinerEngine>(id);
        if (type == "rf_switch_spdt_2to1")
            return make<RFSwitch2to1Engine>(id);
        if (type == "rf_switch_spdt")
            return make<RFSwitchEngine>(id);
        return nullptr;
    }

  private:
    NodeGraphEngine m_graph;
    std::vector<std::unique_ptr<IComponentEngine>> m_owned;

    template <typename T> IComponentEngine *make(int id) {
        auto ptr = std::make_unique<T>(id, m_graph);
        IComponentEngine *raw = ptr.get();
        m_owned.push_back(std::move(ptr));
        return raw;
    }
};

class TestNaHost final : public IMeasurementChainHost {
  public:
    explicit TestNaHost(std::vector<IComponentEngine *> chain) {
        for (auto *comp : chain)
            m_by_node[comp->graphNodeId()] = comp;
    }

    IComponentEngine *componentForNode(int graph_node_id) const override {
        auto it = m_by_node.find(graph_node_id);
        return it == m_by_node.end() ? nullptr : it->second;
    }

    std::unique_ptr<IMeasurementChainScratch> beginScratchPass() const override {
        return std::make_unique<TestNaScratch>();
    }

  private:
    std::map<int, IComponentEngine *> m_by_node;
};

// Issue #169's switched filter bank: a 1:2 switch fans out to two parallel
// branches that rejoin at a 2:1 switch. Attenuators stand in for the filters
// so expected values are exact; both switches keep their 0.5 dB insertion
// loss and 40 dB isolation unless a test changes them.
struct FilterBank {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen{1, graph};
    RFSwitchEngine fan_out{2, graph};    // COM -> T1/T2
    AttenuatorEngine branch_1{3, graph}; // on T1
    AttenuatorEngine branch_2{4, graph}; // on T2
    RFSwitch2to1Engine fan_in{5, graph}; // T1/T2 -> COM
    TestNaHost host{{&gen, &fan_out, &branch_1, &branch_2, &fan_in}};

    FilterBank() {
        branch_1.setAttenuation(3.0);
        branch_2.setAttenuation(10.0);
        graph.addLink(gen.outputPinId(), fan_out.inputPinId(0));
        graph.addLink(fan_out.outputPinId(0), branch_1.inputPinId());
        graph.addLink(fan_out.outputPinId(1), branch_2.inputPinId());
        graph.addLink(branch_1.outputPinId(), fan_in.inputPinId(0));
        graph.addLink(branch_2.outputPinId(), fan_in.inputPinId(1));
    }

    void setThrows(int fan_out_throw, int fan_in_throw) {
        fan_out.setActiveThrow(fan_out_throw);
        fan_in.setActiveThrow(fan_in_throw);
    }

    void setIsolation(double dB) {
        fan_out.setIsolation_dB(dB);
        fan_in.setIsolation_dB(dB);
    }
};
namespace {
// Local copies of NonlinearModel's dBm<->linear unit helpers (detail::).
double dbmToW(double dBm) { return std::pow(10.0, dBm / 10.0) * 0.001; }
double dbmToV(double dBm) { return std::sqrt(dbmToW(dBm) * 50.0); }
double vToDbm(double V) { return 10.0 * std::log10((V * V / 50.0) / 0.001); }
} // namespace

// Replicates NonlinearModel::process() for a single gain stage (same math,
// same order: per-tone harmonics first, then IMD pairs among the first 3
// tones), returning the compression_dB the engine would apply. Used to verify
// that the configured stimulus power — not some fixed level — actually reaches
// the isolated chain: gain through a compressed amplifier is
// gain_dB + compression(P_stim).
double expectedCompressionDb(const std::vector<Spectrum::Tone> &input_tones, double gain_dB,
                             double oip2_dBm, double oip3_dBm) {
    const double gain_linear = std::pow(10.0, gain_dB / 20.0);
    const double k1 = 1.0 / dbmToV(oip2_dBm);
    const double k2 = 4.0 / (3.0 * dbmToV(oip3_dBm) * dbmToV(oip3_dBm));

    double total_distortion = 0.0; // W (model accumulates W despite the name)
    for (const auto &tone : input_tones) {
        const double Pout_dBm = tone.power_dBm + 20.0 * std::log10(gain_linear);
        const double Vp1 = dbmToV(Pout_dBm);
        total_distortion += dbmToW(vToDbm(k1 * Vp1 * Vp1 / std::sqrt(2.0))); // H2
        total_distortion += dbmToW(vToDbm(k2 * Vp1 * Vp1 * Vp1 / 2.0));      // H3
    }
    const int n = std::min(static_cast<int>(input_tones.size()), 3);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const double P1 =
                input_tones[static_cast<size_t>(i)].power_dBm + 20.0 * std::log10(gain_linear);
            const double P2 =
                input_tones[static_cast<size_t>(j)].power_dBm + 20.0 * std::log10(gain_linear);
            const double Vp1 = dbmToV(P1);
            const double Vp2 = dbmToV(P2);
            total_distortion += 2.0 * dbmToW(vToDbm(k1 * Vp1 * Vp2));                     // IM2
            total_distortion += 2.0 * dbmToW(vToDbm((3.0 / 4.0) * k2 * Vp1 * Vp1 * Vp2)); // IM3
            total_distortion += 2.0 * dbmToW(vToDbm((3.0 / 4.0) * k2 * Vp1 * Vp2 * Vp2)); // IM3
        }
    }
    double pfund = 0.0; // W
    for (const auto &tone : input_tones)
        pfund += dbmToW(tone.power_dBm + 20.0 * std::log10(gain_linear));

    if (total_distortion >= pfund || pfund <= 0.0)
        return -1e9;
    return 10.0 * std::log10(1.0 - total_distortion / pfund);
}

} // namespace

TEST_CASE("MeasurementChainRunner: one prepared chain accepts successive stimuli",
          "[network_analyzer][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    atten.setAttenuation(10.0);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    TestNaHost host({&gen, &atten});

    auto path = findMeasurementChainPath(graph, host, gen.outputPinId(), atten.outputPinId());
    REQUIRE(path.has_value());
    IsolatedChainRunner runner(host);
    REQUIRE(runner.prepare(*path));

    Spectrum first;
    first.tones.push_back({1e9, -20.0, 0.0});
    Spectrum second;
    second.tones.push_back({1e9, -35.0, 0.0});
    const Spectrum *first_result = runner.run(first);
    REQUIRE(first_result != nullptr);
    REQUIRE(first_result->tones.size() == 1);
    REQUIRE_THAT(first_result->tones[0].power_dBm, WithinAbs(-30.0, 0.05));
    const Spectrum *second_result = runner.run(second);
    REQUIRE(second_result != nullptr);
    REQUIRE(second_result->tones.size() == 1);
    REQUIRE_THAT(second_result->tones[0].power_dBm, WithinAbs(-45.0, 0.05));
}

TEST_CASE("MeasurementChainRunner: path preserves exact multi-output ports",
          "[network_analyzer][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    SplitterEngine splitter(2, graph);
    AttenuatorEngine atten(3, graph);
    graph.addLink(gen.outputPinId(), splitter.inputPinId());
    graph.addLink(splitter.outputPinId(1), atten.inputPinId());
    TestNaHost host({&gen, &splitter, &atten});
    auto point_b_path =
        findMeasurementChainPath(graph, host, gen.outputPinId(), splitter.outputPinId(1));
    REQUIRE(point_b_path.has_value());
    REQUIRE(point_b_path->edges.size() == 1);
    REQUIRE(point_b_path->edges[0].output_port == 0);
    REQUIRE(point_b_path->point_b_output_port == 1);
    IsolatedChainRunner point_b_runner(host);
    REQUIRE(point_b_runner.prepare(*point_b_path));
    Spectrum stimulus;
    stimulus.tones.push_back({1e9, -20.0, 0.0});
    const Spectrum *point_b_result = point_b_runner.run(stimulus);
    REQUIRE(point_b_result != nullptr);
    REQUIRE(point_b_result->tones.size() == 1);
    REQUIRE_THAT(point_b_result->tones[0].power_dBm, WithinAbs(-23.0103, 0.05));

    auto edge_path = findMeasurementChainPath(graph, host, gen.outputPinId(), atten.outputPinId());
    REQUIRE(edge_path.has_value());
    REQUIRE(edge_path->edges.size() == 2);
    REQUIRE(edge_path->edges[1].output_port == 1);
    IsolatedChainRunner edge_runner(host);
    REQUIRE(edge_runner.prepare(*edge_path));
    const Spectrum *edge_result = edge_runner.run(stimulus);
    REQUIRE(edge_result != nullptr);
    REQUIRE_THAT(edge_result->tones[0].power_dBm, WithinAbs(-23.0103, 0.05));
}

TEST_CASE("MeasurementChainRunner: singly fed RF switch path is supported",
          "[network_analyzer][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    RFSwitch2to1Engine sw(2, graph);
    sw.setActiveThrow(1);
    graph.addLink(gen.outputPinId(), sw.inputPinId(1));
    TestNaHost host({&gen, &sw});
    auto path = findMeasurementChainPath(graph, host, gen.outputPinId(), sw.outputPinId());
    REQUIRE(path.has_value());
    REQUIRE(path->edges.size() == 1);
    REQUIRE(path->edges[0].input_port == 1);
    IsolatedChainRunner runner(host);
    REQUIRE(runner.prepare(*path));
    Spectrum stimulus;
    stimulus.tones.push_back({1e9, -20.0, 0.0});
    const Spectrum *result = runner.run(stimulus);
    REQUIRE(result != nullptr);
    REQUIRE(result->tones.size() == 1);
    REQUIRE_THAT(result->tones[0].power_dBm, WithinAbs(-20.5, 0.05));
}

TEST_CASE("MeasurementChainRunner: unsupported circuits are rejected",
          "[network_analyzer][runner]") {
    SECTION("combiner") {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen(1, graph);
        CombinerEngine combiner(2, graph);
        AttenuatorEngine end(3, graph);
        graph.addLink(gen.outputPinId(), combiner.inputPinId(0));
        graph.addLink(combiner.outputPinId(), end.inputPinId());
        TestNaHost host({&gen, &combiner, &end});
        REQUIRE_FALSE(findMeasurementChainPath(graph, host, gen.outputPinId(), end.outputPinId()));
    }
    SECTION("combiner rejoining a fan-out") {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen(1, graph);
        SplitterEngine splitter(2, graph);
        AttenuatorEngine branch_a(3, graph);
        AttenuatorEngine branch_b(4, graph);
        CombinerEngine combiner(5, graph);
        graph.addLink(gen.outputPinId(), splitter.inputPinId());
        graph.addLink(splitter.outputPinId(0), branch_a.inputPinId());
        graph.addLink(splitter.outputPinId(1), branch_b.inputPinId());
        graph.addLink(branch_a.outputPinId(), combiner.inputPinId(0));
        graph.addLink(branch_b.outputPinId(), combiner.inputPinId(1));
        TestNaHost host({&gen, &splitter, &branch_a, &branch_b, &combiner});
        REQUIRE_FALSE(
            findMeasurementChainPath(graph, host, gen.outputPinId(), combiner.outputPinId()));
    }
    SECTION("switch throw fed from outside the circuit") {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen_a(1, graph);
        SignalGeneratorEngine gen_b(2, graph);
        RFSwitch2to1Engine sw(3, graph);
        graph.addLink(gen_a.outputPinId(), sw.inputPinId(0));
        graph.addLink(gen_b.outputPinId(), sw.inputPinId(1));
        TestNaHost host({&gen_a, &gen_b, &sw});
        REQUIRE_FALSE(findMeasurementChainPath(graph, host, gen_a.outputPinId(), sw.outputPinId()));
    }
    SECTION("two links into one input") {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen(1, graph);
        SplitterEngine splitter(2, graph);
        AttenuatorEngine branch_a(3, graph);
        AttenuatorEngine branch_b(4, graph);
        AttenuatorEngine end(5, graph);
        graph.addLink(gen.outputPinId(), splitter.inputPinId());
        graph.addLink(splitter.outputPinId(0), branch_a.inputPinId());
        graph.addLink(splitter.outputPinId(1), branch_b.inputPinId());
        graph.addLink(branch_a.outputPinId(), end.inputPinId());
        graph.addLink(branch_b.outputPinId(), end.inputPinId());
        TestNaHost host({&gen, &splitter, &branch_a, &branch_b, &end});
        REQUIRE_FALSE(findMeasurementChainPath(graph, host, gen.outputPinId(), end.outputPinId()));
    }
    SECTION("cycle") {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen(1, graph);
        RFSwitch2to1Engine sw(2, graph);
        AttenuatorEngine feedback(3, graph);
        graph.addLink(gen.outputPinId(), sw.inputPinId(0));
        graph.addLink(sw.outputPinId(), feedback.inputPinId());
        graph.addLink(feedback.outputPinId(), sw.inputPinId(1));
        TestNaHost host({&gen, &sw, &feedback});
        REQUIRE_FALSE(
            findMeasurementChainPath(graph, host, gen.outputPinId(), feedback.outputPinId()));
    }
}

TEST_CASE("MeasurementChainRunner: switched filter bank runs both branches",
          "[network_analyzer][runner][issue169]") {
    FilterBank bank;
    bank.setThrows(0, 0);
    auto path = findMeasurementChainPath(bank.graph, bank.host, bank.gen.outputPinId(),
                                         bank.fan_in.outputPinId());
    REQUIRE(path.has_value());
    // Topological order with ties broken by graph order: Point A's generator,
    // the fan-out, both branches, then Point B's switch.
    const std::vector<IComponentEngine *> expected_order{&bank.gen, &bank.fan_out, &bank.branch_1,
                                                         &bank.branch_2, &bank.fan_in};
    REQUIRE(path->components == expected_order);
    // Every link keeps its exact ports, so both 2:1 throws get their own
    // branch: (from, output port, to, input port) in components[] indices.
    using Wire = std::tuple<size_t, int, size_t, int>;
    std::vector<Wire> wires;
    for (const auto &edge : path->edges)
        wires.emplace_back(edge.from, edge.output_port, edge.to, edge.input_port);
    const std::vector<Wire> expected_wires{
        {0, 0, 1, 0}, {1, 0, 2, 0}, {1, 1, 3, 0}, {2, 0, 4, 0}, {3, 0, 4, 1}};
    REQUIRE(wires == expected_wires);

    IsolatedChainRunner runner(bank.host);
    REQUIRE(runner.prepare(*path));
    Spectrum stimulus;
    stimulus.tones.push_back({1e9, -20.0, 0.0});
    const Spectrum *result = runner.run(stimulus);
    REQUIRE(result != nullptr);
    // The selected T1 branch at insertion loss comes first, then the T2
    // branch leaking through both switches' isolation.
    REQUIRE(result->tones.size() == 2);
    REQUIRE_THAT(result->tones[0].power_dBm, WithinAbs(-20.0 - 0.5 - 3.0 - 0.5, 1e-9));
    REQUIRE_THAT(result->tones[1].power_dBm, WithinAbs(-20.0 - 40.0 - 10.0 - 40.0, 1e-9));
}

TEST_CASE("MeasurementChainRunner: Point A feeds the measured circuit through one output",
          "[network_analyzer][runner][issue169]") {
    FilterBank bank;
    // Point A names a component, not a pin: picked on the fan-out's T2 output
    // pin, the T1 branch is still measured through the T1 output feeding it.
    const auto one_branch = findMeasurementChainPath(
        bank.graph, bank.host, bank.fan_out.outputPinId(1), bank.branch_1.outputPinId());
    REQUIRE(one_branch.has_value());
    REQUIRE(one_branch->edges.size() == 1);
    REQUIRE(one_branch->edges[0].output_port == 0);
    // Both fan-out outputs feed the 2:1 switch. The stimulus would replace
    // both alike and ignore the fan-out's own throw, so there is no faithful
    // measurement from there.
    REQUIRE_FALSE(findMeasurementChainPath(bank.graph, bank.host, bank.fan_out.outputPinId(0),
                                           bank.fan_in.outputPinId()));
}

TEST_CASE("MeasurementChainRunner: prepare refuses a path it cannot wire faithfully",
          "[network_analyzer][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine first(2, graph);
    RFSwitch2to1Engine sw(3, graph);
    TestNaHost host({&gen, &first, &sw});
    using Edge = MeasurementChainPath::Edge;
    // Baseline: the stimulus feeds `first`, which feeds the switch's T1.
    MeasurementChainPath path;
    path.components = {&gen, &first, &sw};
    path.edges = {Edge{0, 0, 1, 0}, Edge{1, 0, 2, 0}};
    IsolatedChainRunner runner(host);
    REQUIRE(runner.prepare(path));

    SECTION("an edge into Point A's component") {
        path.edges.push_back(Edge{1, 0, 0, 0});
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("an input port the clone does not have") {
        path.edges[1].input_port = 2;
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("an output port the clone does not have") {
        path.edges[1].output_port = 1;
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("an edge running against the clone order") {
        // The stimulus feeds the switch, whose output feeds `first`, listed
        // before it: `first` would run on the switch's stale output.
        path.edges = {Edge{0, 0, 2, 0}, Edge{2, 0, 1, 0}};
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("two edges into one input") {
        path.edges.push_back(Edge{0, 0, 2, 0});
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("no edge carrying the stimulus") {
        path.edges.erase(path.edges.begin());
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("a stimulus edge from an output Point A does not have") {
        path.edges[0].output_port = 1;
        REQUIRE_FALSE(runner.prepare(path));
        path.edges[0].output_port = -1;
        REQUIRE_FALSE(runner.prepare(path));
    }
    SECTION("no Point A component") {
        path.components[0] = nullptr;
        REQUIRE_FALSE(runner.prepare(path));
    }
}

TEST_CASE("MeasurementChainRunner: scratch run does not mutate live engine state",
          "[network_analyzer][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    gen.addTone(1e9, -20.0);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    gen.update(0.0);
    atten.node().inputs[0] = &gen.node().outputs[0];
    atten.update(0.0);
    TestNaHost host({&gen, &atten});
    const auto gen_state = gen.serialize();
    const auto atten_state = atten.serialize();
    const Spectrum gen_output_before = gen.node().outputs[0];
    const Spectrum atten_output_before = atten.node().outputs[0];
    const Spectrum *const live_input_before = atten.node().inputs[0];
    const size_t link_count_before = graph.links().size();
    auto path = findMeasurementChainPath(graph, host, gen.outputPinId(), atten.outputPinId());
    REQUIRE(path.has_value());
    IsolatedChainRunner runner(host);
    REQUIRE(runner.prepare(*path));
    Spectrum stimulus;
    stimulus.tones.push_back({1e9, -20.0, 0.0});
    REQUIRE(runner.run(stimulus) != nullptr);
    REQUIRE(gen.serialize() == gen_state);
    REQUIRE(atten.serialize() == atten_state);
    REQUIRE(atten.node().inputs[0] == live_input_before);
    REQUIRE(graph.links().size() == link_count_before);
    REQUIRE(gen.node().outputs[0].tones.size() == gen_output_before.tones.size());
    REQUIRE(atten.node().outputs[0].tones.size() == atten_output_before.tones.size());
    REQUIRE(gen.node().outputs[0].tones[0].power_dBm == gen_output_before.tones[0].power_dBm);
    REQUIRE(atten.node().outputs[0].tones[0].power_dBm == atten_output_before.tones[0].power_dBm);
}

// ---------------------------------------------------------------------------
// 1. Stimulus correctness — points() frequencies, evenly spaced start->stop.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: sweep frequencies are evenly spaced start to stop",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    TestNaHost host({&gen, &atten});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(21);
    na.setPointA(gen.outputPinId());
    na.setPointB(atten.outputPinId());
    na.update();

    const auto &freqs = na.sweepFrequencies();
    REQUIRE(freqs.size() == 21);
    REQUIRE_THAT(freqs.front(), WithinAbs(1e9, 1.0));
    REQUIRE_THAT(freqs.back(), WithinAbs(2e9, 1.0));
    const double step = (2e9 - 1e9) / 20.0;
    for (size_t i = 1; i < freqs.size(); ++i)
        REQUIRE_THAT(freqs[i] - freqs[i - 1], WithinAbs(step, 1.0));
}

TEST_CASE("NetworkAnalyzer: configured stimulus power reaches the isolated chain",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AmplifierEngine amp(2, graph);
    amp.setGain_dB(20.0);
    amp.setNF_dB(0.0);
    amp.setEnableNonlinear(true);
    amp.setOIP3_dBm(0.0); // OIP2 stays at the 100 dBm default
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    TestNaHost host({&gen, &amp});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(2);
    na.setPointA(gen.outputPinId());
    na.setPointB(amp.outputPinId());

    // The measured gain through a compressed amplifier is
    // gain_dB + compression(P_stim); expectedCompressionDb() replicates the
    // model's voltage-gain convention, so matching it pins the ABSOLUTE
    // stimulus power — a fixed-level stimulus would produce a different gain
    // at a different configured power.
    const auto run_and_check = [&](double stim_dBm) {
        na.setStimulusPower(stim_dBm);
        na.update();
        std::vector<Spectrum::Tone> input;
        for (double f : na.sweepFrequencies())
            input.push_back({f, stim_dBm, 0.0});
        const double expected = 20.0 + expectedCompressionDb(input, 20.0, 100.0, 0.0);
        REQUIRE(na.gainDb().size() == 2);
        for (double g : na.gainDb())
            REQUIRE_THAT(g, WithinAbs(expected, 0.01));
        return expected;
    };

    // -40 dBm: near-linear regime, gain ~= 20 dB.
    run_and_check(-40.0);
    // -30 dBm: measurably compressed — a clearly different gain than above,
    // exactly per the model, proving the sweep runs at the configured power.
    const double compressed = run_and_check(-30.0);
    REQUIRE_THAT(compressed, WithinAbs(19.8925, 0.01));

    // Deeply into compression the model saturates (MIN_POWER) -> no data.
    na.setStimulusPower(30.0);
    na.update();
    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
}

// ---------------------------------------------------------------------------
// 2. Gain accuracy — Generator -> Attenuator(10 dB), A/B on their real output
//    pins, real link between them: gainDb == -10 dB across the sweep.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: gain accuracy against a real attenuator chain", "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    atten.setAttenuation(10.0);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    TestNaHost host({&gen, &atten});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(21);
    na.setStimulusPower(-30.0);
    na.setPointA(gen.outputPinId());
    na.setPointB(atten.outputPinId());
    na.update();

    REQUIRE(na.gainDb().size() == 21);
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(-10.0, 0.05));
    // A passive pad's noise figure equals its attenuation.
    for (double nf : na.noiseFigureDb())
        REQUIRE_THAT(nf, WithinAbs(10.0, 0.1));
}

// ---------------------------------------------------------------------------
// 3. NF accuracy — Generator -> Amplifier(manual gain/NF), A/B on their
//    outputs: noiseFigureDb == configured NF across the sweep.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: noise figure accuracy against a real amplifier chain",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AmplifierEngine amp(2, graph);
    amp.setGain_dB(20.0);
    amp.setNF_dB(5.0);
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    TestNaHost host({&gen, &amp});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(21);
    na.setStimulusPower(-30.0);
    na.setPointA(gen.outputPinId());
    na.setPointB(amp.outputPinId());
    na.update();

    REQUIRE(na.gainDb().size() == 21);
    for (double nf : na.noiseFigureDb())
        REQUIRE_THAT(nf, WithinAbs(5.0, 0.1));
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(20.0, 0.05));
}

// ---------------------------------------------------------------------------
// 4. Non-invasiveness — a real second consumer already reads a component's
//    output; probing that same output as Point A leaves the real consumer's
//    measured tones/noise bit-identical before and after the NA runs.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: probing does not perturb a real consumer", "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    gen.addTone(1e9, -20.0);
    SplitterEngine splitter(2, graph);
    AttenuatorEngine consumer(3, graph); // real second consumer of splitter OUT1
    AttenuatorEngine branch(4, graph);   // downstream of splitter OUT2 -> Point B
    graph.addLink(gen.outputPinId(), splitter.inputPinId());
    graph.addLink(splitter.outputPinId(), consumer.inputPinId());
    graph.addLink(splitter.outputPinId(1), branch.inputPinId());
    // NodeGraphEngine links are topology-only; wire the real execution paths.
    splitter.node().inputs[0] = &gen.node().outputs[0];
    consumer.node().inputs[0] = &splitter.node().outputs[0];
    branch.node().inputs[0] = &splitter.node().outputs[1];

    TestNaHost host({&gen, &splitter, &consumer, &branch});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);

    const auto run_real_chain = [&]() {
        gen.update(0.0);
        splitter.update(0.0);
        consumer.update(0.0);
        branch.update(0.0);
    };

    // Baseline: the real chain runs with no NA probing anywhere.
    run_real_chain();
    const Spectrum baseline = consumer.node().outputs[0]; // copy snapshot

    // Probe: Point A = splitter OUT1 — the very output the real consumer
    // reads; Point B = branch output (unique path splitter -> branch). The NA
    // must measure on a private clone chain, leaving every real component
    // untouched.
    na.setPointA(splitter.outputPinId());
    na.setPointB(branch.outputPinId());
    na.update();

    // The consumer's output is bit-identical (exact equality, not Approx).
    const Spectrum &after = consumer.node().outputs[0];
    REQUIRE(after.frequencies == baseline.frequencies);
    REQUIRE(after.noise_total_W == baseline.noise_total_W);
    REQUIRE(after.tones.size() == baseline.tones.size());
    for (size_t i = 0; i < baseline.tones.size(); ++i) {
        REQUIRE(after.tones[i].freq_Hz == baseline.tones[i].freq_Hz);
        REQUIRE(after.tones[i].power_dBm == baseline.tones[i].power_dBm);
        REQUIRE(after.tones[i].phase_deg == baseline.tones[i].phase_deg);
    }

    // Re-running the real chain after the NA pass still reproduces the
    // baseline (the NA must not have written into any real input/output).
    run_real_chain();
    const Spectrum &rerun = consumer.node().outputs[0];
    REQUIRE(rerun.tones.size() == baseline.tones.size());
    for (size_t i = 0; i < baseline.tones.size(); ++i)
        REQUIRE(rerun.tones[i].power_dBm == baseline.tones[i].power_dBm);

    // The NA measurement itself is valid on the unique splitter->branch path
    // (the splitter itself is upstream of Point A and excluded from the chain).
    REQUIRE_FALSE(na.gainDb().empty());
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(0.0, 0.05)); // branch attenuator at 0 dB
}

// ---------------------------------------------------------------------------
// 5. No path — Point A/B on disconnected components -> all-NaN.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: disconnected Point B yields all-NaN", "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    AttenuatorEngine isolated(3, graph);
    graph.addLink(gen.outputPinId(), atten.inputPinId()); // isolated: no link
    TestNaHost host({&gen, &atten, &isolated});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(isolated.outputPinId());
    na.update();

    REQUIRE(na.gainDb().size() == 11);
    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

// ---------------------------------------------------------------------------
// 6. Two links into one input — a Splitter fans out to two branches that
//    both drive Point B's single input pin. The clones cannot merge two feeds
//    into one pin (the editor's canAddLink() never creates one) -> all-NaN.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: splitter branches rejoining on one input pin yield all-NaN",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    SplitterEngine splitter(2, graph);
    AttenuatorEngine branch_a(3, graph);
    AttenuatorEngine branch_b(4, graph);
    AttenuatorEngine end(5, graph); // Point B, reached via both branches
    graph.addLink(gen.outputPinId(), splitter.inputPinId());
    graph.addLink(splitter.outputPinId(), branch_a.inputPinId());
    graph.addLink(splitter.outputPinId(1), branch_b.inputPinId());
    graph.addLink(branch_a.outputPinId(), end.inputPinId());
    graph.addLink(branch_b.outputPinId(), end.inputPinId());
    TestNaHost host({&gen, &splitter, &branch_a, &branch_b, &end});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(end.outputPinId());
    na.update();

    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

// ---------------------------------------------------------------------------
// 7. Combiner in path — the only route from A to B crosses a Combiner's
//    combined signal output -> all-NaN.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: path through a Combiner yields all-NaN", "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    CombinerEngine combiner(3, graph);
    AttenuatorEngine end(4, graph); // Point B
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    graph.addLink(atten.outputPinId(), combiner.inputPinId());
    graph.addLink(combiner.outputPinId(), end.inputPinId());
    TestNaHost host({&gen, &atten, &combiner, &end});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(end.outputPinId());
    na.update();

    // Sanity: the graph really does connect A to B (via the combiner) — so
    // the all-NaN result is the combiner rejection, not a missing link.
    REQUIRE(graph.nodeIdForPin(combiner.outputPinId()) == combiner.graphNodeId());
    REQUIRE(graph.nodeIdForPin(end.inputPinId()) == end.graphNodeId());
    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

// ---------------------------------------------------------------------------
// 8. RF Switch 2:1 in path — the analyzer must preserve the input port used
//    by the discovered graph path, so its stimulus reaches the selected throw
//    even when that is T2.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: path through an RF Switch 2:1 follows the selected throw",
          "[network_analyzer]") {
    const auto measure_through_throw = [](int throw_index, int input_port) {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen(1, graph);
        RFSwitch2to1Engine sw(2, graph);
        sw.setActiveThrow(throw_index);
        graph.addLink(gen.outputPinId(), sw.inputPinId(input_port));
        TestNaHost host({&gen, &sw});
        NetworkAnalyzerEngine na(graph, host);
        na.setStartFrequency(1e9);
        na.setStopFrequency(2e9);
        na.setPoints(11);
        na.setPointA(gen.outputPinId());
        na.setPointB(sw.outputPinId());
        na.update();

        REQUIRE(graph.nodeIdForPin(sw.outputPinId()) == sw.graphNodeId());
        REQUIRE(graph.nodeIdForPin(gen.outputPinId()) == gen.graphNodeId());
        INFO("entered through input port " << input_port << " with throw " << throw_index);
        REQUIRE(na.gainDb().size() == 11);
        for (double g : na.gainDb()) {
            const double expected_gain = (throw_index == input_port) ? -0.5 : -40.0;
            REQUIRE_THAT(g, WithinAbs(expected_gain, 0.05));
        }
        // The isolation-path NF includes the switch's added-noise term:
        // 10*log10((1 - G_IL) / G_ISO) ~= 30.36 dB for the shipped defaults.
        for (double nf : na.noiseFigureDb()) {
            const double expected_nf = (throw_index == input_port) ? 0.5 : 30.36;
            REQUIRE_THAT(nf, WithinAbs(expected_nf, 0.1));
        }
    };

    measure_through_throw(1, 1);
    measure_through_throw(0, 0);
    measure_through_throw(0, 1);
    measure_through_throw(1, 0);
}

// The other throw is driven by an independent live source outside the
// Point A -> Point B circuit. The private clones cannot reproduce it, so the
// measurement is rejected rather than reported without that signal/noise.
TEST_CASE("NetworkAnalyzer: RF Switch 2:1 throw fed by a second live source yields all-NaN",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine probed_gen(1, graph);
    SignalGeneratorEngine active_gen(2, graph);
    RFSwitch2to1Engine sw(3, graph);
    sw.setActiveThrow(0); // T1 is active; the probed signal arrives on T2.
    AttenuatorEngine end(4, graph);
    graph.addLink(active_gen.outputPinId(), sw.inputPinId(0));
    graph.addLink(probed_gen.outputPinId(), sw.inputPinId(1));
    graph.addLink(sw.outputPinId(), end.inputPinId());
    TestNaHost host({&probed_gen, &active_gen, &sw, &end});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(probed_gen.outputPinId());
    na.setPointB(end.outputPinId());
    na.update();

    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

// ---------------------------------------------------------------------------
// 8b. Switched filter bank (issue #169): both branches are part of the
//     measured circuit, so the clones reproduce the live engines exactly:
//     Gain follows the selected throws (the 2:1 switch emits its selected
//     throw's tone first and the analyzer reads the first tone), and NF
//     includes the unselected branch's noise leaking through isolation.
// ---------------------------------------------------------------------------
namespace {

struct BankMeasurement {
    std::vector<double> gain_dB;
    std::vector<double> nf_dB;
};

BankMeasurement measureFilterBank(FilterBank &bank) {
    NetworkAnalyzerEngine na(bank.graph, bank.host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(bank.gen.outputPinId());
    na.setPointB(bank.fan_in.outputPinId());
    na.update();
    return {na.gainDb(), na.noiseFigureDb()};
}

// Independent reference: drive the LIVE engines with a stimulus identical to
// the analyzer's (same grid, -30 dBm tones, kT noise) and derive Gain/NF from
// the 2:1 switch output the same way the analyzer does (first tone at each
// sweep frequency, NF = noise / (G kT)). Leaving `drop_unselected_branch`
// false wires both throws, as the live circuit is; true leaves the fan-in's
// unselected input open, which is what a selected-path-only clone would see.
BankMeasurement liveReference(FilterBank &bank, bool drop_unselected_branch) {
    Spectrum stim;
    for (int i = 0; i < 11; ++i) {
        const double f = 1e9 + 1e9 * i / 10.0;
        stim.frequencies.push_back(f);
        stim.tones.push_back({f, -30.0, 0.0});
    }
    stim.noise_W.assign(11, k * T);
    stim.noise_added_W.assign(11, 0.0);
    stim.phase_deg.assign(11, 0.0);
    stim.computeTotalNoise();
    stim.bumpGeneration();

    // The live engines borrow pointers to `stim`, a local; clear them on
    // every exit, including a failed REQUIRE below, so nothing dangles.
    struct ClearInputs {
        FilterBank &bank;
        ~ClearInputs() {
            for (IComponentEngine *engine : std::initializer_list<IComponentEngine *>{
                     &bank.fan_out, &bank.branch_1, &bank.branch_2, &bank.fan_in})
                std::fill(engine->node().inputs.begin(), engine->node().inputs.end(), nullptr);
        }
    } clear_inputs{bank};
    bank.fan_out.node().inputs[0] = &stim;
    bank.branch_1.node().inputs[0] = &bank.fan_out.node().outputs[0];
    bank.branch_2.node().inputs[0] = &bank.fan_out.node().outputs[1];
    bank.fan_in.node().inputs[0] = &bank.branch_1.node().outputs[0];
    bank.fan_in.node().inputs[1] = &bank.branch_2.node().outputs[0];
    if (drop_unselected_branch)
        bank.fan_in.node().inputs[1 - bank.fan_in.activeThrow()] = nullptr;
    for (IComponentEngine *engine : std::initializer_list<IComponentEngine *>{
             &bank.fan_out, &bank.branch_1, &bank.branch_2, &bank.fan_in})
        engine->update(0.0);

    const Spectrum &out = bank.fan_in.node().outputs[0];
    BankMeasurement ref;
    for (size_t i = 0; i < stim.frequencies.size(); ++i) {
        const double f = stim.frequencies[i];
        const auto tone = std::find_if(out.tones.begin(), out.tones.end(), [&](const auto &t) {
            return std::abs(t.freq_Hz - f) <= 1.0;
        });
        REQUIRE(tone != out.tones.end());
        const double gain_dB = tone->power_dBm + 30.0;
        ref.gain_dB.push_back(gain_dB);
        ref.nf_dB.push_back(10.0 *
                            std::log10(out.noise_total_W[i] / dbToLinear(gain_dB) / (k * T)));
    }
    return ref;
}

void requireMatchesLive(FilterBank &bank, double expected_gain_dB) {
    const BankMeasurement na = measureFilterBank(bank);
    const BankMeasurement live = liveReference(bank, false);
    REQUIRE(na.gain_dB.size() == 11);
    for (size_t i = 0; i < na.gain_dB.size(); ++i) {
        REQUIRE_THAT(na.gain_dB[i], WithinAbs(expected_gain_dB, 1e-9));
        REQUIRE_THAT(na.gain_dB[i], WithinAbs(live.gain_dB[i], 1e-9));
        REQUIRE_THAT(na.nf_dB[i], WithinAbs(live.nf_dB[i], 1e-9));
    }
}

} // namespace

TEST_CASE("NetworkAnalyzer: switched filter bank measures through the selected throws",
          "[network_analyzer][issue169]") {
    FilterBank bank;

    SECTION("both switches on T1 measure the T1 branch") {
        bank.setThrows(0, 0);
        requireMatchesLive(bank, -0.5 - 3.0 - 0.5);
    }
    SECTION("both switches on T2 measure the T2 branch") {
        bank.setThrows(1, 1);
        requireMatchesLive(bank, -0.5 - 10.0 - 0.5);
    }
    SECTION("mismatched throws report the selected fan-in throw at isolation") {
        // Fan-out on T1, fan-in on T2: the fan-in's selected input carries
        // the T2 branch, which the fan-out only feeds at isolation. The
        // stronger T1-branch leakage (-0.5 - 3 - 40 = -43.5 dB) arrives at
        // the same frequency on the unselected throw and is not summed into
        // Gain (first-match), but its noise is in the NF numerator.
        bank.setThrows(0, 1);
        requireMatchesLive(bank, -40.0 - 10.0 - 0.5);
    }
}

TEST_CASE("NetworkAnalyzer: switched filter bank NF includes unselected-branch noise",
          "[network_analyzer][issue169]") {
    FilterBank bank;
    bank.setThrows(0, 0);
    // Low isolation makes the unselected branch's noise contribution
    // measurable: a selected-path-only clone would under-report NF.
    bank.setIsolation(10.0);

    const BankMeasurement na = measureFilterBank(bank);
    const BankMeasurement full = liveReference(bank, false);
    const BankMeasurement selected_only = liveReference(bank, true);
    REQUIRE(na.nf_dB.size() == 11);
    for (size_t i = 0; i < na.nf_dB.size(); ++i) {
        REQUIRE_THAT(na.nf_dB[i], WithinAbs(full.nf_dB[i], 1e-9));
        REQUIRE(na.nf_dB[i] - selected_only.nf_dB[i] > 0.05);
    }
}

TEST_CASE("NetworkAnalyzer: switched filter bank fed from outside the measured circuit yields "
          "all-NaN",
          "[network_analyzer][issue169]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    RFSwitchEngine fan_out(2, graph);
    AttenuatorEngine branch(3, graph);
    RFSwitch2to1Engine fan_in(4, graph);
    SignalGeneratorEngine other(5, graph); // a second live source on the fan-in's T2
    graph.addLink(gen.outputPinId(), fan_out.inputPinId(0));
    graph.addLink(fan_out.outputPinId(0), branch.inputPinId());
    graph.addLink(branch.outputPinId(), fan_in.inputPinId(0));
    graph.addLink(other.outputPinId(), fan_in.inputPinId(1));
    TestNaHost host({&gen, &fan_out, &branch, &fan_in, &other});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(fan_in.outputPinId());
    na.update();

    REQUIRE(na.gainDb().size() == 11);
    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

// ---------------------------------------------------------------------------
// 9. Mixer in path — the clone reproduces the configured lo_freq_Hz
//    translation (LO is a parameter copied by deserialize(), not a live wired
//    signal). With LO == exactly one grid step, every sweep tone's upper/lower
//    sideband lands back on the grid, so every point is matched at the
//    conversion gain; a misaligned LO leaves every point unmatched -> all-NaN.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: mixer LO translation is reproduced on the clone chain",
          "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    MixerEngine mixer(2, graph);
    mixer.setLoFreq_Hz(100e6); // == the 100 MHz grid step below
    mixer.setConversionGain_dB(0.0);
    mixer.setNF_dB(3.0);
    graph.addLink(gen.outputPinId(), mixer.inputPinId());
    TestNaHost host({&gen, &mixer});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11); // grid step = (2e9-1e9)/10 = 100 MHz, exact in doubles
    na.setPointA(gen.outputPinId());
    na.setPointB(mixer.outputPinId());
    na.update();

    REQUIRE(na.gainDb().size() == 11);
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(0.0, 0.05));
    for (double nf : na.noiseFigureDb())
        REQUIRE_THAT(nf, WithinAbs(3.0, 0.1));

    // Misaligned LO: translated tones land between grid points -> no match.
    mixer.setLoFreq_Hz(99e6);
    na.update();
    for (double g : na.gainDb())
        REQUIRE(std::isnan(g));
    for (double nf : na.noiseFigureDb())
        REQUIRE(std::isnan(nf));
}

// ---------------------------------------------------------------------------
// Engine state basics.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: points clamped to [2, 2001]", "[network_analyzer]") {
    NodeGraphEngine graph;
    TestNaHost host({});
    NetworkAnalyzerEngine na(graph, host);

    na.setPoints(1);
    REQUIRE(na.points() == 2);

    na.setPoints(5000);
    REQUIRE(na.points() == 2001);

    na.setPoints(201);
    REQUIRE(na.points() == 201);
}

TEST_CASE("NetworkAnalyzer: engine serialize/deserialize round-trip", "[network_analyzer]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    TestNaHost host({&gen, &atten});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(2.4e9);
    na.setStopFrequency(2.5e9);
    na.setPoints(51);
    na.setStimulusPower(-15.0);
    na.setPointA(gen.outputPinId());
    na.setPointB(atten.outputPinId());

    const auto j = na.serialize();

    NetworkAnalyzerEngine restored(graph, host);
    restored.deserialize(j);
    REQUIRE(restored.startFrequency() == 2.4e9);
    REQUIRE(restored.stopFrequency() == 2.5e9);
    REQUIRE(restored.points() == 51);
    REQUIRE(restored.stimulusPower() == -15.0);
    REQUIRE(restored.pointAPin() == gen.outputPinId());
    REQUIRE(restored.pointBPin() == atten.outputPinId());
}

// ---------------------------------------------------------------------------
// Widget smoke test — RAII ImGui/ImPlot contexts (DestroyContext on every
// path, including assertion failures: the fixture destructor always runs).
// ---------------------------------------------------------------------------
#include "app.h"
#include "imgui.h"
#include "imnodes.h"
#include "implot.h"
// The axis ranges ImPlot settles on are only observable through its internals
// (the pinned implot commit makes that stable).
#include "implot_internal.h"
namespace {
struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImNodes::CreateContext();
        // A bare ImGui context starts with a (-1,-1) DisplaySize sentinel and a
        // default imgui.ini path; give the widget a real frame to draw into and
        // keep the test run pristine (CWD is the repo root).
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        // No renderer backend here, so the font atlas must be built explicitly
        // (NewFrame() asserts TexIsBuilt when RendererHasTextures is not set).
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0;
        int atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~ImGuiFixture() {
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};
} // namespace

TEST_CASE_METHOD(ImGuiFixture, "NetworkAnalyzer: app scratch adapter measures switch COM output",
                 "[network_analyzer][app]") {
    RfSimulatorApp app;
    auto &gen = static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    auto &sw =
        static_cast<RFSwitch2to1Engine &>(*app.testCreateComponent("rf_switch_spdt_2to1", 10002));
    REQUIRE(app.testConnectLink(gen.outputPinId(), sw.inputPinId(0)).has_value());

    auto &na = app.testNetworkAnalyzerEngine();
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(sw.outputPinId());
    na.update();

    REQUIRE(na.gainDb().size() == 11);
    for (double gain : na.gainDb())
        REQUIRE_THAT(gain, WithinAbs(-0.5, 0.05));
    for (double nf : na.noiseFigureDb())
        REQUIRE_THAT(nf, WithinAbs(0.5, 0.1));
}

// Issue #169 through the production clone factory: both switch types and
// both branches are cloned by the app's ComponentTypeRegistry-backed scratch.
TEST_CASE_METHOD(ImGuiFixture, "NetworkAnalyzer: app measures a switched filter bank",
                 "[network_analyzer][app][issue169]") {
    RfSimulatorApp app;
    auto &gen = static_cast<SignalGeneratorEngine &>(*app.testCreateComponent("generator", 10001));
    auto &fan_out =
        static_cast<RFSwitchEngine &>(*app.testCreateComponent("rf_switch_spdt", 10002));
    auto &branch_1 = static_cast<AttenuatorEngine &>(*app.testCreateComponent("attenuator", 10003));
    auto &branch_2 = static_cast<AttenuatorEngine &>(*app.testCreateComponent("attenuator", 10004));
    auto &fan_in =
        static_cast<RFSwitch2to1Engine &>(*app.testCreateComponent("rf_switch_spdt_2to1", 10005));
    branch_1.setAttenuation(3.0);
    branch_2.setAttenuation(10.0);
    REQUIRE(app.testConnectLink(gen.outputPinId(), fan_out.inputPinId(0)).has_value());
    REQUIRE(app.testConnectLink(fan_out.outputPinId(0), branch_1.inputPinId()).has_value());
    REQUIRE(app.testConnectLink(fan_out.outputPinId(1), branch_2.inputPinId()).has_value());
    REQUIRE(app.testConnectLink(branch_1.outputPinId(), fan_in.inputPinId(0)).has_value());
    REQUIRE(app.testConnectLink(branch_2.outputPinId(), fan_in.inputPinId(1)).has_value());

    auto &na = app.testNetworkAnalyzerEngine();
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(fan_in.outputPinId());

    const auto require_gain = [&](double expected_dB) {
        na.update();
        REQUIRE(na.gainDb().size() == 11);
        for (double gain : na.gainDb())
            REQUIRE_THAT(gain, WithinAbs(expected_dB, 1e-9));
        for (double nf : na.noiseFigureDb())
            REQUIRE(std::isfinite(nf));
    };

    fan_out.setActiveThrow(0);
    fan_in.setActiveThrow(0);
    require_gain(-0.5 - 3.0 - 0.5);

    // Flipping both throws must invalidate the cached signature and
    // re-measure through the other branch.
    fan_out.setActiveThrow(1);
    fan_in.setActiveThrow(1);
    require_gain(-0.5 - 10.0 - 0.5);
}

TEST_CASE_METHOD(ImGuiFixture, "NetworkAnalyzer: widget draws with and without probe points",
                 "[network_analyzer][widget]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    atten.setAttenuation(10.0);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    TestNaHost host({&gen, &atten});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);
    na.setPointA(gen.outputPinId());
    na.setPointB(atten.outputPinId());
    na.update();

    NetworkAnalyzerWidget widget(na, graph);
    bool open = true;
    ImGui::NewFrame();
    widget.draw("Network Analyzer Test", &open);
    REQUIRE(open);
    ImGui::EndFrame();

    // Unset points: the pickers fall back to "(none)", the plot draws all-NaN.
    na.setPointA(-1);
    na.setPointB(-1);
    na.update();
    ImGui::NewFrame();
    widget.draw("Network Analyzer Test", &open);
    REQUIRE(open);
    ImGui::EndFrame();
}

namespace {
// The plot object lives in the window that owns the plot id, so re-enter that
// window for the lookup.
ImPlotRange plotAxisRange(const char *window, const char *plot, ImAxis axis) {
    ImGui::Begin(window);
    ImPlotPlot *p = ImPlot::GetPlot(plot);
    REQUIRE(p != nullptr);
    const ImPlotRange range = p->Axes[axis].Range;
    ImGui::End();
    return range;
}
} // namespace

// ImPlot only auto-fits a plot on the frame it is first drawn and never
// again, so the gain/NF traces used to stay pinned to whatever range happened
// to be current then -- including ImPlot's 0..1 default when the panel was
// opened before any probe point was selected, which squeezed the measured
// data into a corner of the plot.
TEST_CASE_METHOD(ImGuiFixture, "NetworkAnalyzer: gain/NF plot scales to the measured sweep",
                 "[network_analyzer][widget]") {
    constexpr const char *kWindow = "Network Analyzer Scaling Test";
    constexpr const char *kPlot = "Gain / Noise Figure vs Frequency";

    NodeGraphEngine graph;
    SignalGeneratorEngine gen(1, graph);
    AttenuatorEngine atten(2, graph);
    atten.setAttenuation(10.0);
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    TestNaHost host({&gen, &atten});
    NetworkAnalyzerEngine na(graph, host);
    na.setStartFrequency(1e9);
    na.setStopFrequency(2e9);
    na.setPoints(11);

    NetworkAnalyzerWidget widget(na, graph);
    bool open = true;

    // Frame 1: the panel is opened before any probe point is selected, so the
    // plot is created with no data to fit. The size is pinned because a
    // freshly created auto-sized window leaves the plot no rect to draw into.
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_Always);
    widget.draw(kWindow, &open);
    ImGui::EndFrame();
    REQUIRE(open);

    // Frame 2: probing a 1-2 GHz sweep with a 10 dB pad must frame that sweep
    // on X and the -10 dB gain/NF traces on Y.
    na.setPointA(gen.outputPinId());
    na.setPointB(atten.outputPinId());
    na.update();
    ImGui::NewFrame();
    widget.draw(kWindow, &open);
    const ImPlotRange x = plotAxisRange(kWindow, kPlot, ImAxis_X1);
    const ImPlotRange y = plotAxisRange(kWindow, kPlot, ImAxis_Y1);
    ImGui::EndFrame();

    REQUIRE_THAT(x.Min, WithinAbs(1e9, 1.0));
    REQUIRE_THAT(x.Max, WithinAbs(2e9, 1.0));
    REQUIRE(y.Min <= -10.0);
    REQUIRE(y.Max >= -10.0);
    REQUIRE(y.Max - y.Min < 100.0); // framed on the data, not a stale wide range

    // Frame 3: changing the sweep re-frames the plot -- the stale-range half
    // of the same bug.
    na.setStartFrequency(2.4e9);
    na.setStopFrequency(2.5e9);
    na.update();
    ImGui::NewFrame();
    widget.draw(kWindow, &open);
    const ImPlotRange moved = plotAxisRange(kWindow, kPlot, ImAxis_X1);
    ImGui::EndFrame();

    REQUIRE_THAT(moved.Min, WithinAbs(2.4e9, 1.0));
    REQUIRE_THAT(moved.Max, WithinAbs(2.5e9, 1.0));

    // Frame 4: a flat trace (0 dB pad -- constant gain and NF) still gets a
    // readable dB window rather than a zero-height axis.
    atten.setAttenuation(0.0);
    na.update();
    REQUIRE_FALSE(na.gainDb().empty());
    for (double g : na.gainDb())
        REQUIRE_THAT(g, WithinAbs(na.gainDb().front(), 1e-9)); // constant across the sweep
    ImGui::NewFrame();
    widget.draw(kWindow, &open);
    const ImPlotRange flat = plotAxisRange(kWindow, kPlot, ImAxis_Y1);
    ImGui::EndFrame();

    REQUIRE(flat.Min <= 0.0);
    REQUIRE(flat.Max >= 0.0);
    REQUIRE(flat.Max - flat.Min >= 10.0); // never a degenerate dB axis
}
