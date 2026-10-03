#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "measurement_chain_runner.h"
#include "node_graph_engine.h"
#include "receiver_performance_measurement.h"
#include "receiver_requirements.h"
#include "signal_generator_engine.h"
#include <algorithm>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {
enum class DroppedTone {
    None,
    LowerIm3,
    LowerIm3OnlyTwoLevels,
    LowerIm3AtOneLevel,
    UpperIm3,
    LowerFundamental,
    DuplicateSamePhase,
    DuplicateQuadrature
};

class TestAmplifier final : public AmplifierEngine {
  public:
    TestAmplifier(int id, NodeGraphEngine &graph, DroppedTone dropped)
        : AmplifierEngine(id, graph), m_dropped(dropped) {}
    void update(double dt) override {
        AmplifierEngine::update(dt);
        if (node().inputs.empty() || !node().inputs[0] || node().outputs.empty())
            return;
        const auto &tones = node().inputs[0]->tones;
        if (tones.size() == 1 && (m_dropped == DroppedTone::DuplicateSamePhase ||
                                  m_dropped == DroppedTone::DuplicateQuadrature)) {
            auto &output_tones = node().outputs[0].tones;
            const auto fundamental = std::find_if(
                output_tones.begin(), output_tones.end(), [&tones](const Spectrum::Tone &tone) {
                    return std::abs(tone.freq_Hz - tones.front().freq_Hz) < 1.0;
                });
            if (fundamental != output_tones.end()) {
                auto duplicate = *fundamental;
                if (m_dropped == DroppedTone::DuplicateQuadrature)
                    duplicate.phase_deg += 90.0;
                output_tones.push_back(duplicate);
            }
            return;
        }
        if (m_dropped == DroppedTone::None || tones.size() != 2)
            return;
        const double low = tones[0].freq_Hz;
        const double high = tones[1].freq_Hz;
        if (m_dropped == DroppedTone::LowerIm3OnlyTwoLevels && tones[0].power_dBm <= -78.0)
            return;
        if (m_dropped == DroppedTone::LowerIm3AtOneLevel &&
            std::abs(tones[0].power_dBm + 60.0) > 1e-9)
            return;
        const double target = m_dropped == DroppedTone::LowerIm3 ||
                                      m_dropped == DroppedTone::LowerIm3OnlyTwoLevels ||
                                      m_dropped == DroppedTone::LowerIm3AtOneLevel
                                  ? 2.0 * low - high
                              : m_dropped == DroppedTone::UpperIm3 ? 2.0 * high - low
                                                                   : low;
        auto &output_tones = node().outputs[0].tones;
        std::erase_if(output_tones, [target](const Spectrum::Tone &tone) {
            return std::abs(tone.freq_Hz - target) < 1.0;
        });
    }

  private:
    DroppedTone m_dropped;
};

class Scratch final : public IMeasurementChainScratch {
  public:
    explicit Scratch(DroppedTone dropped) : m_dropped(dropped) {}
    IComponentEngine *createClone(std::string_view type, int id) override {
        if (type == "attenuator")
            return make<AttenuatorEngine>(id);
        if (type == "amplifier" && m_dropped != DroppedTone::None)
            return make<TestAmplifier>(id, m_dropped);
        if (type == "amplifier")
            return make<AmplifierEngine>(id);
        if (type == "generator")
            return make<SignalGeneratorEngine>(id);
        return nullptr;
    }

  private:
    DroppedTone m_dropped;
    NodeGraphEngine graph;
    std::vector<std::unique_ptr<IComponentEngine>> owned;
    template <typename T, typename... Args> IComponentEngine *make(int id, Args &&...args) {
        auto ptr = std::make_unique<T>(id, graph, std::forward<Args>(args)...);
        auto *result = ptr.get();
        owned.push_back(std::move(ptr));
        return result;
    }
};

class Host final : public IMeasurementChainHost {
  public:
    explicit Host(std::vector<IComponentEngine *> components,
                  DroppedTone dropped = DroppedTone::None)
        : m_dropped(dropped) {
        for (auto *component : components)
            by_node[component->graphNodeId()] = component;
    }
    IComponentEngine *componentForNode(int node) const override {
        const auto it = by_node.find(node);
        return it == by_node.end() ? nullptr : it->second;
    }
    std::unique_ptr<IMeasurementChainScratch> beginScratchPass() const override {
        ++scratch_passes;
        return std::make_unique<Scratch>(m_dropped);
    }
    mutable int scratch_passes = 0;

  private:
    DroppedTone m_dropped;
    std::map<int, IComponentEngine *> by_node;
};

struct Circuit {
    NodeGraphEngine graph;
    SignalGeneratorEngine generator{1, graph};
    AttenuatorEngine attenuator{2, graph};
    AmplifierEngine amplifier{3, graph};
    Host host{{&generator, &attenuator, &amplifier}};
    Circuit() {
        graph.addLink(generator.outputPinId(), attenuator.inputPinId());
        graph.addLink(attenuator.outputPinId(), amplifier.inputPinId());
        generator.addTone(1.0e9, -10.0);
    }
    int pointB() const { return amplifier.outputPinId(); }
};

ReceiverRequirementsConfig config(double frequency = 1.0e9) {
    ReceiverRequirementsConfig c;
    c.band_start_Hz = 1.0e9;
    c.band_stop_Hz = 2.0e9;
    c.output_power = ReceiverOutputPowerLimits{-100.0, 100.0};
    c.iip3_min_dBm = 0.0;
    c.measurement_conditions.output_reference_tone_frequency_Hz = frequency;
    c.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -60.0, -40.0, 2.0};
    return c;
}

bool isFinite(double value) { return std::isfinite(value); }
} // namespace

TEST_CASE("Receiver measurement engine measures selected generator-reference output power",
          "[receiver_measurements]") {
    Circuit c;
    c.attenuator.setAttenuation(7.0);
    c.amplifier.setGain_dB(12.0);
    const auto before = c.generator.serialize();
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    engine.update(config(), c.generator.outputPinId(), c.pointB(), {1.0e9, 1.1e9});
    const auto &m = engine.measurements();
    REQUIRE(m.output_power_dBm.size() == 2);
    CHECK(m.output_power_dBm[0] == Catch::Approx(-5.0).margin(1e-8));
    CHECK(m.output_power_dBm[1] == Catch::Approx(-5.0).margin(1e-8));
    CHECK(c.generator.serialize() == before);
}

TEST_CASE("Receiver output power coherently combines duplicate-frequency tones by phase",
          "[receiver_measurements]") {
    Circuit c;
    auto selected = config();
    selected.iip3_min_dBm.reset();

    ReceiverPerformanceMeasurementEngine single(c.graph, c.host);
    single.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    REQUIRE(isFinite(single.measurements().output_power_dBm[0]));
    const double single_power = single.measurements().output_power_dBm[0];

    Host same_phase_host{{&c.generator, &c.attenuator, &c.amplifier},
                         DroppedTone::DuplicateSamePhase};
    ReceiverPerformanceMeasurementEngine same_phase(c.graph, same_phase_host);
    same_phase.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(same_phase.measurements().output_power_dBm[0] ==
          Catch::Approx(single_power + 6.020599913).margin(1e-6));

    Host quadrature_host{{&c.generator, &c.attenuator, &c.amplifier},
                         DroppedTone::DuplicateQuadrature};
    ReceiverPerformanceMeasurementEngine quadrature(c.graph, quadrature_host);
    quadrature.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(quadrature.measurements().output_power_dBm[0] ==
          Catch::Approx(single_power + 3.010299957).margin(1e-6));
}

TEST_CASE("Receiver source resolves one tone automatically and explicit selection among tones",
          "[receiver_measurements]") {
    Circuit c;
    c.generator.addTone(1.1e9, -20.0);
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    auto selected = config(1.1e9);
    selected.iip3_min_dBm.reset();
    engine.update(selected, c.generator.outputPinId(), c.attenuator.outputPinId(), {1.0e9});
    CHECK(engine.measurements().output_power_dBm[0] == Catch::Approx(-20.0));
    selected.measurement_conditions.output_reference_tone_frequency_Hz = 1.0e9;
    engine.update(selected, c.generator.outputPinId(), c.attenuator.outputPinId(), {1.0e9});
    CHECK(engine.measurements().output_power_dBm[0] == Catch::Approx(-10.0));
    selected.measurement_conditions.output_reference_tone_frequency_Hz = 1.2e9;
    engine.update(selected, c.generator.outputPinId(), c.attenuator.outputPinId(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
}

TEST_CASE("Receiver measurement rejects missing, removed, duplicate and ambiguous sources",
          "[receiver_measurements]") {
    Circuit c;
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    auto selected = config();
    selected.iip3_min_dBm.reset();
    engine.update(selected, -1, c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
    selected.measurement_conditions.output_reference_tone_frequency_Hz.reset();
    c.generator.addTone(1.1e9, -20.0);
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
    c.generator.removeTone(1);
    c.generator.addTone(1.0e9, -11.0);
    engine.update(config(), c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
    c.generator.removeTone(1);
    engine.update(config(1.0e9), c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(isFinite(engine.measurements().output_power_dBm[0]));
    c.generator.updateTone(0, 1.2e9, -10.0);
    selected = config(1.0e9);
    selected.iip3_min_dBm.reset();
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
}

TEST_CASE("Receiver source requires a generator and a resolvable tone", "[receiver_measurements]") {
    Circuit c;
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    auto selected = config();
    selected.iip3_min_dBm.reset();
    selected.measurement_conditions.output_reference_tone_frequency_Hz.reset();
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(engine.measurements().output_power_dBm[0] == Catch::Approx(-10.0));
    c.generator.removeTone(0);
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
    c.generator.addTone(1.0e9, -10.0);
    engine.update(selected, c.attenuator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
}

TEST_CASE("Receiver IIP3 estimates single and cascaded nonlinear stages on both IM3 sides",
          "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setGain_dB(10.0);
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(40.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -80.0, -50.0, 2.0};
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    engine.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    REQUIRE(isFinite(engine.measurements().iip3_dBm[0]));
    CHECK(engine.measurements().iip3_dBm[0] == Catch::Approx(30.0).margin(1.5));

    auto cascade = std::make_unique<AmplifierEngine>(4, c.graph);
    cascade->setGain_dB(5.0);
    cascade->setEnableNonlinear(true);
    cascade->setOIP3_dBm(36.0);
    cascade->setP1dB_dBm(90.0);
    c.graph.addLink(c.amplifier.outputPinId(), cascade->inputPinId());
    Host cascade_host{{&c.generator, &c.attenuator, &c.amplifier, cascade.get()}};
    ReceiverPerformanceMeasurementEngine cascade_engine(c.graph, cascade_host);
    cascade_engine.update(settings, c.generator.outputPinId(), cascade->outputPinId(), {1.0e9});
    REQUIRE(isFinite(cascade_engine.measurements().iip3_dBm[0]));
    const double expected =
        -10.0 * std::log10(std::pow(10.0, -30.0 / 10.0) + std::pow(10.0, -21.0 / 10.0));
    CHECK(cascade_engine.measurements().iip3_dBm[0] == Catch::Approx(expected).margin(2.0));
    CHECK(receiverIIP3LevelCount(*settings.measurement_conditions.iip3) == 16);
}
TEST_CASE("Receiver IIP3 preserves the analytic estimate with 1 Hz tone spacing",
          "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setGain_dB(10.0);
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(40.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -80.0, -50.0, 2.0};

    ReceiverPerformanceMeasurementEngine ordinary_spacing(c.graph, c.host);
    ordinary_spacing.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    REQUIRE(isFinite(ordinary_spacing.measurements().iip3_dBm[0]));

    settings.measurement_conditions.iip3->tone_spacing_Hz = 1.0;
    ReceiverPerformanceMeasurementEngine one_hz_spacing(c.graph, c.host);
    one_hz_spacing.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    REQUIRE(isFinite(one_hz_spacing.measurements().iip3_dBm[0]));
    CHECK(one_hz_spacing.measurements().iip3_dBm[0] == Catch::Approx(30.0).margin(1.5));
    CHECK(one_hz_spacing.measurements().iip3_dBm[0] ==
          Catch::Approx(ordinary_spacing.measurements().iip3_dBm[0]).margin(1e-8));
}

TEST_CASE("Receiver measurement skips disabled metrics across a full sweep",
          "[receiver_measurements]") {
    Circuit c;
    auto disabled = config();
    disabled.output_power.reset();
    disabled.iip3_min_dBm.reset();

    std::vector<double> sweep;
    constexpr std::size_t points = 2001;
    sweep.reserve(points);
    for (std::size_t i = 0; i < points; ++i)
        sweep.push_back(1.0e9 + static_cast<double>(i) * 5.0e5);

    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    engine.update(disabled, c.generator.outputPinId(), c.pointB(), sweep);
    CHECK_FALSE(engine.isInProgress());
    REQUIRE(engine.measurements().output_power_dBm.size() == points);
    CHECK(std::all_of(engine.measurements().output_power_dBm.begin(),
                      engine.measurements().output_power_dBm.end(),
                      [](double power) { return std::isnan(power); }));
}

TEST_CASE("Receiver IIP3 measurement leaves disabled output power unavailable",
          "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setGain_dB(10.0);
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(40.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto iip3_only = config();
    iip3_only.output_power.reset();
    iip3_only.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -80.0, -50.0, 2.0};

    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    engine.update(iip3_only, c.generator.outputPinId(), c.pointB(), {1.0e9});
    REQUIRE(isFinite(engine.measurements().iip3_dBm[0]));
    REQUIRE(engine.measurements().output_power_dBm.size() == 1);
    CHECK(std::isnan(engine.measurements().output_power_dBm[0]));
}

TEST_CASE("Receiver IIP3 retains fixed spacing and rejects unsupported edges and invalid fits",
          "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(35.0);
    c.amplifier.setP1dB_dBm(90.0);
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{20.0e6, -60.0, -40.0, 2.0};
    engine.update(settings, c.generator.outputPinId(), c.pointB(), {5.0e6, 10.0e6, 1.1e9});
    CHECK(std::isnan(engine.measurements().iip3_dBm[0]));
    CHECK(std::isnan(engine.measurements().iip3_dBm[1]));
    CHECK(isFinite(engine.measurements().iip3_dBm[2]));
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, 20.0, 40.0, 2.0};
    engine.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().iip3_dBm[0]));
    CHECK(receiverIIP3LevelCount(*settings.measurement_conditions.iip3) == 11);
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -50.0, -40.0, 0.09};
    CHECK_FALSE(receiverIIP3LevelCount(*settings.measurement_conditions.iip3).has_value());
}

TEST_CASE("Receiver IIP3 is unavailable when fundamentals or IM3 are absent or invalid",
          "[receiver_measurements]") {
    Circuit c;
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -60.0, -40.0, 2.0};
    // A linear stage produces fundamentals but no third-order products.
    engine.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().iip3_dBm[0]));
    settings.measurement_conditions.iip3->input_step_dB = 0.0;
    engine.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().iip3_dBm[0]));
    CHECK(receiverIIP3LevelCount(*settings.measurement_conditions.iip3) == std::nullopt);
}

TEST_CASE("Receiver IIP3 rejects a missing fundamental and requires both IM3 side fits",
          "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(40.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -80.0, -50.0, 2.0};
    ReceiverPerformanceMeasurementEngine reference(c.graph, c.host);
    reference.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    REQUIRE(isFinite(reference.measurements().iip3_dBm[0]));

    Host missing_side{{&c.generator, &c.attenuator, &c.amplifier}, DroppedTone::LowerIm3};
    ReceiverPerformanceMeasurementEngine one_side(c.graph, missing_side);
    one_side.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(one_side.measurements().iip3_dBm[0]));

    Host one_missing_level{{&c.generator, &c.attenuator, &c.amplifier},
                           DroppedTone::LowerIm3AtOneLevel};
    ReceiverPerformanceMeasurementEngine one_missing_level_engine(c.graph, one_missing_level);
    one_missing_level_engine.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(isFinite(one_missing_level_engine.measurements().iip3_dBm[0]));

    Host insufficient_side{{&c.generator, &c.attenuator, &c.amplifier},
                           DroppedTone::LowerIm3OnlyTwoLevels};
    ReceiverPerformanceMeasurementEngine insufficient(c.graph, insufficient_side);
    insufficient.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(insufficient.measurements().iip3_dBm[0]));

    Host missing_fundamental{{&c.generator, &c.attenuator, &c.amplifier},
                             DroppedTone::LowerFundamental};
    ReceiverPerformanceMeasurementEngine no_fundamental(c.graph, missing_fundamental);
    no_fundamental.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(no_fundamental.measurements().iip3_dBm[0]));
}

TEST_CASE("Receiver IIP3 rejects a compressed nonlinear power range", "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setGain_dB(10.0);
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(20.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -20.0, 20.0, 2.0};
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    engine.update(settings, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(std::isnan(engine.measurements().iip3_dBm[0]));
}

TEST_CASE("Receiver measurement cache reuses unchanged request and invalidates changed request",
          "[receiver_measurements]") {
    Circuit c;
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    auto selected = config();
    selected.iip3_min_dBm.reset();
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    const int prepared = c.host.scratch_passes;
    const double result = engine.measurements().output_power_dBm[0];
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(c.host.scratch_passes == prepared);
    CHECK(engine.measurements().output_power_dBm[0] == result);
    c.attenuator.setAttenuation(1.0);
    engine.update(selected, c.generator.outputPinId(), c.pointB(), {1.0e9});
    CHECK(c.host.scratch_passes > prepared);
}
TEST_CASE("Receiver measurement resumes incremental in-band work and resets changed requests",
          "[receiver_measurements]") {
    Circuit c;
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(40.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -120.0, -20.0, 1.0};
    REQUIRE(receiverIIP3LevelCount(*settings.measurement_conditions.iip3) == 101);

    std::vector<double> full_band_grid;
    constexpr std::size_t points = 1000;
    full_band_grid.reserve(points);
    for (std::size_t i = 0; i < points; ++i)
        full_band_grid.push_back(1.0e9 + static_cast<double>(i) * (1.0e9 / (points - 1)));

    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    engine.update(settings, c.generator.outputPinId(), c.pointB(), full_band_grid);
    CHECK(engine.isInProgress());
    const auto &partial = engine.measurements();
    REQUIRE(partial.output_power_dBm.size() == points);
    REQUIRE(partial.iip3_dBm.size() == points);
    CHECK(isFinite(partial.output_power_dBm.front()));
    CHECK(std::isnan(partial.output_power_dBm.back()));
    CHECK(std::isnan(partial.iip3_dBm.back()));

    ReceiverRequirementsState state;
    state.config = settings;
    std::vector<double> unavailable(points, std::numeric_limits<double>::quiet_NaN());
    const auto evaluation = evaluateReceiverRequirements(
        state, full_band_grid.front(), full_band_grid.back(), full_band_grid, unavailable,
        unavailable, partial.output_power_dBm, partial.iip3_dBm);
    CHECK(evaluation.overall == ReceiverRequirementStatus::Incomplete);

    const int prepared_passes = c.host.scratch_passes;
    engine.update(settings, c.generator.outputPinId(), c.pointB(), full_band_grid);
    CHECK(engine.isInProgress());
    CHECK(c.host.scratch_passes == prepared_passes);

    settings.measurement_conditions.output_reference_tone_frequency_Hz = 1.2e9;
    engine.update(settings, c.generator.outputPinId(), c.pointB(), full_band_grid);
    CHECK(engine.isInProgress());
    CHECK(c.host.scratch_passes > prepared_passes);
    const auto &reset = engine.measurements();
    REQUIRE(reset.output_power_dBm.size() == points);
    REQUIRE(reset.iip3_dBm.size() == points);
    CHECK(std::isnan(reset.output_power_dBm.front()));
    CHECK(std::isnan(reset.output_power_dBm.back()));
}

TEST_CASE("Receiver IIP3 benchmark covers maximum 101 levels on full analyzer grid",
          "[.bench][bench]") {
    Circuit c;
    c.amplifier.setEnableNonlinear(true);
    c.amplifier.setOIP3_dBm(40.0);
    c.amplifier.setP1dB_dBm(90.0);
    auto settings = config();
    settings.measurement_conditions.iip3 = ReceiverIIP3TestSettings{2.0e6, -120.0, -20.0, 1.0};
    REQUIRE(receiverIIP3LevelCount(*settings.measurement_conditions.iip3) == 101);
    std::vector<double> full_grid;
    constexpr std::size_t points = 2001;
    for (std::size_t i = 0; i < points; ++i)
        full_grid.push_back(1.0e9 + static_cast<double>(i) * (5.0e9 / 2000.0));
    ReceiverPerformanceMeasurementEngine engine(c.graph, c.host);
    BENCHMARK("full-grid 101-level receiver recompute") {
        settings.band_start_Hz += 1.0; // force a fresh request on every benchmark sample
        engine.update(settings, c.generator.outputPinId(), c.pointB(), full_grid);
        for (int update = 0; engine.isInProgress() && update < 1000; ++update)
            engine.update(settings, c.generator.outputPinId(), c.pointB(), full_grid);
        REQUIRE_FALSE(engine.isInProgress());
        return engine.measurements().iip3_dBm;
    };
    BENCHMARK("unchanged cached full-grid 101-level update") {
        engine.update(settings, c.generator.outputPinId(), c.pointB(), full_grid);
        return engine.measurements().iip3_dBm;
    };
}
