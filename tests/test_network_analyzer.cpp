// Network Analyzer v3 — engine + widget tests (Task 3 rewrite).
//
// The v3 engine is a singleton instrument panel, not an IComponentEngine: it
// is constructed directly with (NodeGraphEngine&, INetworkAnalyzerHost&) and
// measures a chain of REAL graph components on a private, throwaway clone
// chain ("cheat" mode — the real simulation is never read for signal purposes
// and never written to; see network_analyzer_engine.h). These tests build
// small real DUT chains (plain engines + NodeGraphEngine links), inject a
// test-local INetworkAnalyzerHost, and verify the measurement semantics from
// the design spec.
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "combiner_engine.h"
#include "mixer_engine.h"
#include "network_analyzer_engine.h"
#include "network_analyzer_widget.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"
#include "spectrum.h"
#include "splitter_engine.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <string_view>
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
class TestNaScratch final : public INetworkAnalyzerScratch {
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

class TestNaHost final : public INetworkAnalyzerHost {
  public:
    explicit TestNaHost(std::vector<IComponentEngine *> chain) {
        for (auto *comp : chain)
            m_by_node[comp->graphNodeId()] = comp;
    }

    IComponentEngine *componentForNode(int graph_node_id) const override {
        auto it = m_by_node.find(graph_node_id);
        return it == m_by_node.end() ? nullptr : it->second;
    }

    std::unique_ptr<INetworkAnalyzerScratch> beginScratchPass() const override {
        return std::make_unique<TestNaScratch>();
    }

  private:
    std::map<int, IComponentEngine *> m_by_node;
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
    const double gain_linear = std::pow(10.0, gain_dB / 10.0);
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
    amp.setOIP3_dBm(20.0); // OIP2 stays at the 100 dBm default
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
    // model's math exactly (including its 20*log10(linear) power convention),
    // so matching it pins the ABSOLUTE stimulus power — a fixed-level stimulus
    // would produce a different gain at a different configured power.
    const auto run_and_check = [&](double stim_dBm) {
        na.setStimulusPower(stim_dBm);
        na.update();
        std::vector<Spectrum::Tone> input;
        for (double f : na.sweepFrequencies())
            input.push_back({f, stim_dBm, 0.0});
        const double expected = 20.0 + expectedCompressionDb(input, 20.0, 100.0, 20.0);
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
    REQUIRE_THAT(compressed, WithinAbs(19.89, 0.01));

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
// 6. Ambiguous path — a Splitter fans out to two branches that both reach
//    Point B (two distinct paths) -> all-NaN.
// ---------------------------------------------------------------------------
TEST_CASE("NetworkAnalyzer: ambiguous splitter fan-out yields all-NaN", "[network_analyzer]") {
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
// 8. Mixer in path — the clone reproduces the configured lo_freq_Hz
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
#include "imgui.h"
#include "implot.h"

namespace {
struct ImGuiFixture {
    ImGuiFixture() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        // A bare ImGui context starts with a (-1,-1) DisplaySize sentinel and a
        // default imgui.ini path; give the widget a real frame to draw into and
        // keep the test run pristine (CWD is the repo root).
        ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
        ImGui::GetIO().IniFilename = nullptr;
        // No renderer backend here, so the font atlas must be built explicitly
        // (NewFrame() asserts TexIsBuilt when RendererHasTextures is not set).
        unsigned char *atlas_pixels = nullptr;
        int atlas_w = 0, atlas_h = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas_pixels, &atlas_w, &atlas_h);
    }
    ~ImGuiFixture() {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};
} // namespace

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
