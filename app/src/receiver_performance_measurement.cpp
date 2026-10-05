#include "receiver_performance_measurement.h"

#include "common.h"
#include "component_interface.h"
#include "measurement_chain_runner.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr double kUnavailable = std::numeric_limits<double>::quiet_NaN();

bool closeFrequency(double actual, double expected,
                    double maximum_tolerance = std::numeric_limits<double>::infinity()) {
    if (!std::isfinite(actual) || !std::isfinite(expected) || !(maximum_tolerance > 0.0))
        return false;
    const double tolerance =
        std::min(std::max(1.0, std::abs(expected) * 1.0e-12), maximum_tolerance);
    return std::abs(actual - expected) <= tolerance;
}

std::optional<double>
tonePower(const Spectrum *spectrum, double frequency_Hz,
          double maximum_tolerance = std::numeric_limits<double>::infinity()) {
    if (!spectrum || !std::isfinite(frequency_Hz))
        return std::nullopt;

    double reference_dBm = -std::numeric_limits<double>::infinity();
    std::vector<const Spectrum::Tone *> matches;
    for (const auto &tone : spectrum->tones) {
        if (!closeFrequency(tone.freq_Hz, frequency_Hz, maximum_tolerance))
            continue;
        if (!std::isfinite(tone.power_dBm) || !std::isfinite(tone.phase_deg))
            return std::nullopt;
        reference_dBm = std::max(reference_dBm, tone.power_dBm);
        matches.push_back(&tone);
    }
    if (matches.empty())
        return std::nullopt;

    // Normalize against the strongest record to avoid overflow/underflow while
    // adding RMS-voltage phasors. Power ratios become voltage ratios in dB.
    double in_phase = 0.0;
    double quadrature = 0.0;
    for (const auto *tone : matches) {
        const double amplitude = std::pow(10.0, (tone->power_dBm - reference_dBm) / 20.0);
        const double phase = tone->phase_deg * (std::acos(-1.0) / 180.0);
        in_phase += amplitude * std::cos(phase);
        quadrature += amplitude * std::sin(phase);
    }
    const double resultant = std::hypot(in_phase, quadrature);
    if (!(resultant > 0.0) || !std::isfinite(resultant))
        return std::nullopt;
    const double power_dBm = reference_dBm + 20.0 * std::log10(resultant);
    return std::isfinite(power_dBm) ? std::optional<double>(power_dBm) : std::nullopt;
}

struct LineFit {
    double slope;
    double intercept;
    double rms_error;
};

std::optional<LineFit> fitLine(const std::vector<std::pair<double, double>> &points) {
    if (points.size() < 3)
        return std::nullopt;
    double mean_x = 0.0;
    double mean_y = 0.0;
    for (const auto &[x, y] : points) {
        if (!std::isfinite(x) || !std::isfinite(y))
            return std::nullopt;
        mean_x += x;
        mean_y += y;
    }
    mean_x /= static_cast<double>(points.size());
    mean_y /= static_cast<double>(points.size());
    double xx = 0.0;
    double xy = 0.0;
    for (const auto &[x, y] : points) {
        xx += (x - mean_x) * (x - mean_x);
        xy += (x - mean_x) * (y - mean_y);
    }
    if (!(xx > 0.0))
        return std::nullopt;
    const double slope = xy / xx;
    const double intercept = mean_y - slope * mean_x;
    double squared_error = 0.0;
    for (const auto &[x, y] : points) {
        const double residual = y - (slope * x + intercept);
        squared_error += residual * residual;
    }
    const double rms_error = std::sqrt(squared_error / static_cast<double>(points.size()));
    if (!std::isfinite(slope) || !std::isfinite(intercept) || !std::isfinite(rms_error))
        return std::nullopt;
    return LineFit{slope, intercept, rms_error};
}

std::optional<double> estimateIIP3(const std::vector<std::pair<double, double>> &fundamental,
                                   const std::vector<std::pair<double, double>> &im3) {
    const auto fundamental_fit = fitLine(fundamental);
    const auto im3_fit = fitLine(im3);
    if (!fundamental_fit || !im3_fit)
        return std::nullopt;

    // The analytic single-stage and cascade fixtures have fundamental and IM3
    // slopes of 1 and 3 dB/dB in their uncompressed region. The ±0.35 slope
    // window and 1 dB RMS residual ceiling admit their measured model deviations
    // while rejecting the compressed/non-cubic -20..20 dBm fixture.
    if (std::abs(fundamental_fit->slope - 1.0) > 0.35 || std::abs(im3_fit->slope - 3.0) > 0.35 ||
        fundamental_fit->rms_error > 1.0 || im3_fit->rms_error > 1.0)
        return std::nullopt;
    const double denominator = im3_fit->slope - fundamental_fit->slope;
    if (!(denominator > 0.0))
        return std::nullopt;
    const double intercept = (fundamental_fit->intercept - im3_fit->intercept) / denominator;
    if (!std::isfinite(intercept))
        return std::nullopt;
    return intercept;
}

// The cache key holds only inputs that change measured samples. Pass/fail
// limits and the band are re-evaluated against retained samples every frame,
// links off the measured path never reach the scratch clones, and the path
// signature already serializes every path component (including Point A's
// generator) and every link between them, so a switched filter bank's throws
// and wiring are covered. Conditions of a disabled metric are omitted because
// update() never reads them.
std::string requestKey(const ReceiverRequirementsConfig &config, int point_a_pin, int point_b_pin,
                       const std::vector<double> &sweep,
                       const std::optional<MeasurementChainPath> &path) {
    const auto &conditions = config.measurement_conditions;
    nlohmann::json key;
    key["point_a"] = point_a_pin;
    key["point_b"] = point_b_pin;
    key["grid"] = sweep;
    key["output_enabled"] = config.output_power.has_value();
    key["tone_selector"] = config.output_power && conditions.output_reference_tone_frequency_Hz
                               ? nlohmann::json(*conditions.output_reference_tone_frequency_Hz)
                               : nlohmann::json(nullptr);
    key["iip3_enabled"] = config.iip3_min_dBm.has_value();
    if (config.iip3_min_dBm && conditions.iip3) {
        const auto &settings = *conditions.iip3;
        key["iip3_settings"] = {{"spacing", settings.tone_spacing_Hz},
                                {"start", settings.input_start_dBm},
                                {"stop", settings.input_stop_dBm},
                                {"step", settings.input_step_dB}};
    } else {
        key["iip3_settings"] = nullptr;
    }
    key["path"] = path ? nlohmann::json(path->signature()) : nlohmann::json(nullptr);
    return key.dump();
}
} // namespace

ReceiverPerformanceMeasurementEngine::ReceiverPerformanceMeasurementEngine(
    const NodeGraphEngine &graph, IMeasurementChainHost &host)
    : m_graph(graph), m_host(host) {}

ReceiverPerformanceMeasurementEngine::~ReceiverPerformanceMeasurementEngine() = default;

void ReceiverPerformanceMeasurementEngine::update(const ReceiverRequirementsConfig &config,
                                                  int point_a_pin, int point_b_pin,
                                                  const std::vector<double> &sweep_frequencies_Hz) {
    const auto path = findMeasurementChainPath(m_graph, m_host, point_a_pin, point_b_pin);
    IComponentEngine *generator_component = nullptr;
    const int source_node = m_graph.nodeIdForPin(point_a_pin);
    if (source_node >= 0)
        generator_component = m_host.componentForNode(source_node);
    auto *generator = generator_component && generator_component->type_name() == "generator"
                          ? dynamic_cast<SignalGeneratorEngine *>(generator_component)
                          : nullptr;
    const std::string key =
        requestKey(config, point_a_pin, point_b_pin, sweep_frequencies_Hz, path);
    const bool same_request = m_has_cached_request && key == m_cached_request;
    if (same_request && !m_in_progress)
        return;

    const auto has_supported_iip3_center = [this](double center) {
        if (!m_iip3_settings || !m_iip3_level_count || !std::isfinite(center))
            return false;
        const double spacing = m_iip3_settings->tone_spacing_Hz;
        if (!std::isfinite(spacing) || !(spacing > 0.0))
            return false;
        const double low = center - spacing / 2.0;
        const double high = center + spacing / 2.0;
        const double lower_im3 = 2.0 * low - high;
        const double upper_im3 = 2.0 * high - low;
        if (!(low > 0.0))
            return false;
        const double frequencies[] = {low, high, lower_im3, upper_im3};
        for (double frequency : frequencies) {
            if (!std::isfinite(frequency) || frequency < MIN_FREQ || frequency > MAX_FREQ)
                return false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = i + 1; j < 4; ++j) {
                if (frequencies[i] == frequencies[j])
                    return false;
            }
        }
        return true;
    };

    if (!same_request) {
        m_runner.reset();
        m_cached_request = key;
        m_has_cached_request = true;
        m_measurements.output_power_dBm.assign(sweep_frequencies_Hz.size(), kUnavailable);
        m_measurements.iip3_dBm.assign(sweep_frequencies_Hz.size(), kUnavailable);
        m_sweep_frequencies_Hz = sweep_frequencies_Hz;
        m_output_tone_power_phase.reset();
        m_iip3_settings.reset();
        m_iip3_level_count.reset();
        m_current_center = 0;
        m_current_iip3_level = 0;
        m_center_stage = CenterStage::OutputTone;
        m_lower_fundamental.clear();
        m_upper_fundamental.clear();
        m_lower_im3.clear();
        m_upper_im3.clear();
        m_in_progress = false;

        if (!path)
            return;
        auto runner = std::make_unique<IsolatedChainRunner>(m_host);
        if (!runner->prepare(*path))
            return;
        m_runner = std::move(runner);

        if (config.output_power && generator) {
            const auto &tones = generator->tones();
            const auto selector = config.measurement_conditions.output_reference_tone_frequency_Hz;
            const Spectrum::Tone *selected_tone = nullptr;
            if (selector || tones.size() == 1) {
                const double frequency = selector ? *selector : tones.front().freq_Hz;
                for (const auto &tone : tones) {
                    if (tone.freq_Hz != frequency)
                        continue;
                    if (selected_tone) {
                        selected_tone = nullptr;
                        break;
                    }
                    selected_tone = &tone;
                }
            }
            if (selected_tone)
                m_output_tone_power_phase =
                    std::pair{selected_tone->power_dBm, selected_tone->phase_deg};
        }

        if (config.iip3_min_dBm && config.measurement_conditions.iip3) {
            m_iip3_settings = config.measurement_conditions.iip3;
            m_iip3_level_count = receiverIIP3LevelCount(*m_iip3_settings);
        }
        if (m_iip3_level_count) {
            m_lower_fundamental.reserve(*m_iip3_level_count);
            m_upper_fundamental.reserve(*m_iip3_level_count);
            m_lower_im3.reserve(*m_iip3_level_count);
            m_upper_im3.reserve(*m_iip3_level_count);
        }

        const bool has_output_work =
            m_output_tone_power_phase && std::isfinite(m_output_tone_power_phase->first) &&
            std::any_of(m_sweep_frequencies_Hz.begin(), m_sweep_frequencies_Hz.end(),
                        [](double center) { return std::isfinite(center) && center > 0.0; });
        const bool has_iip3_work = m_iip3_level_count && std::any_of(m_sweep_frequencies_Hz.begin(),
                                                                     m_sweep_frequencies_Hz.end(),
                                                                     has_supported_iip3_center);
        m_in_progress = has_output_work || has_iip3_work;
        if (!m_in_progress)
            return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
    const auto finish_center = [this]() {
        if (m_iip3_settings && m_iip3_level_count) {
            const auto lower = estimateIIP3(m_lower_fundamental, m_lower_im3);
            const auto upper = estimateIIP3(m_upper_fundamental, m_upper_im3);
            if (lower && upper)
                m_measurements.iip3_dBm[m_current_center] = std::min(*lower, *upper);
        }
        m_lower_fundamental.clear();
        m_upper_fundamental.clear();
        m_lower_im3.clear();
        m_upper_im3.clear();
        m_current_iip3_level = 0;
        m_center_stage = CenterStage::OutputTone;
        ++m_current_center;
        if (m_current_center == m_sweep_frequencies_Hz.size())
            m_in_progress = false;
    };

    while (m_in_progress) {
        if (m_current_center >= m_sweep_frequencies_Hz.size()) {
            m_in_progress = false;
            break;
        }
        const double center = m_sweep_frequencies_Hz[m_current_center];
        if (m_center_stage == CenterStage::OutputTone) {
            if (m_output_tone_power_phase && std::isfinite(m_output_tone_power_phase->first) &&
                std::isfinite(center) && center > 0.0) {
                if (std::chrono::steady_clock::now() >= deadline)
                    break;
                Spectrum stimulus;
                stimulus.generation = ++m_spectrum_generation;
                stimulus.frequencies = {center};
                stimulus.tones.push_back(
                    {center, m_output_tone_power_phase->first, m_output_tone_power_phase->second});
                if (const auto output = tonePower(m_runner->run(stimulus), center))
                    m_measurements.output_power_dBm[m_current_center] = *output;
            }
            m_center_stage = CenterStage::IIP3;
            continue;
        }

        if (!has_supported_iip3_center(center) ||
            m_current_iip3_level >= m_iip3_level_count.value_or(0)) {
            finish_center();
            continue;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            break;

        const auto &settings = *m_iip3_settings;
        const double low = center - settings.tone_spacing_Hz / 2.0;
        const double high = center + settings.tone_spacing_Hz / 2.0;
        const double input_dBm = settings.input_start_dBm +
                                 static_cast<double>(m_current_iip3_level) * settings.input_step_dB;
        Spectrum stimulus;
        stimulus.generation = ++m_spectrum_generation;
        stimulus.frequencies = {center};
        stimulus.tones = {{low, input_dBm, 0.0}, {high, input_dBm, 0.0}};
        const Spectrum *output = m_runner->run(stimulus);
        const double frequency_tolerance = settings.tone_spacing_Hz * 0.25;
        if (const auto power = tonePower(output, low, frequency_tolerance))
            m_lower_fundamental.emplace_back(input_dBm, *power);
        if (const auto power = tonePower(output, high, frequency_tolerance))
            m_upper_fundamental.emplace_back(input_dBm, *power);
        if (const auto power = tonePower(output, 2.0 * low - high, frequency_tolerance))
            m_lower_im3.emplace_back(input_dBm, *power);
        if (const auto power = tonePower(output, 2.0 * high - low, frequency_tolerance))
            m_upper_im3.emplace_back(input_dBm, *power);
        ++m_current_iip3_level;
    }
}
