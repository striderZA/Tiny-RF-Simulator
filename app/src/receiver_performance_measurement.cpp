#include "receiver_performance_measurement.h"

#include "component_interface.h"
#include "measurement_chain_runner.h"
#include "node_graph_engine.h"
#include "signal_generator_engine.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr double kUnavailable = std::numeric_limits<double>::quiet_NaN();

bool closeFrequency(double actual, double expected) {
    if (!std::isfinite(actual) || !std::isfinite(expected))
        return false;
    return std::abs(actual - expected) <= std::max(1.0, std::abs(expected) * 1.0e-12);
}

std::optional<double> tonePower(const Spectrum *spectrum, double frequency_Hz) {
    if (!spectrum || !std::isfinite(frequency_Hz))
        return std::nullopt;

    double reference_dBm = -std::numeric_limits<double>::infinity();
    std::vector<const Spectrum::Tone *> matches;
    for (const auto &tone : spectrum->tones) {
        if (!closeFrequency(tone.freq_Hz, frequency_Hz))
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
    if (std::abs(fundamental_fit->slope - 1.0) > 0.35 ||
        std::abs(im3_fit->slope - 3.0) > 0.35 || fundamental_fit->rms_error > 1.0 ||
        im3_fit->rms_error > 1.0)
        return std::nullopt;
    const double denominator = im3_fit->slope - fundamental_fit->slope;
    if (!(denominator > 0.0))
        return std::nullopt;
    const double intercept = (fundamental_fit->intercept - im3_fit->intercept) / denominator;
    if (!std::isfinite(intercept))
        return std::nullopt;
    return intercept;
}

std::string requestKey(const ReceiverRequirementsConfig &config, int point_a_pin, int point_b_pin,
                       const std::vector<double> &sweep, const NodeGraphEngine &graph,
                       const std::optional<MeasurementChainPath> &path,
                       const IComponentEngine *generator) {
    nlohmann::json key;
    key["point_a"] = point_a_pin;
    key["point_b"] = point_b_pin;
    key["grid"] = sweep;
    key["config"] = {{"band_start", config.band_start_Hz},
                      {"band_stop", config.band_stop_Hz},
                      {"gain", config.gain ? nlohmann::json{{"min", config.gain->minimum_dB},
                                                             {"max", config.gain->maximum_dB}}
                                            : nlohmann::json(nullptr)},
                      {"nf", config.nf_max_dB ? nlohmann::json(*config.nf_max_dB)
                                              : nlohmann::json(nullptr)},
                      {"output", config.output_power
                                     ? nlohmann::json{{"min", config.output_power->minimum_dBm},
                                                      {"max", config.output_power->maximum_dBm}}
                                     : nlohmann::json(nullptr)},
                      {"iip3_limit", config.iip3_min_dBm ? nlohmann::json(*config.iip3_min_dBm)
                                                         : nlohmann::json(nullptr)},
                      {"tone_selector", config.measurement_conditions
                                                .output_reference_tone_frequency_Hz
                                            ? nlohmann::json(*config.measurement_conditions
                                                                  .output_reference_tone_frequency_Hz)
                                            : nlohmann::json(nullptr)}};
    if (config.measurement_conditions.iip3) {
        const auto &settings = *config.measurement_conditions.iip3;
        key["iip3_settings"] = {{"spacing", settings.tone_spacing_Hz},
                                {"start", settings.input_start_dBm},
                                {"stop", settings.input_stop_dBm},
                                {"step", settings.input_step_dB}};
    } else {
        key["iip3_settings"] = nullptr;
    }
    key["topology"] = nlohmann::json::array();
    for (const auto &link : graph.links())
        key["topology"].push_back({link.start_pin_id, link.end_pin_id});
    if (path) {
        key["path"] = path->signature();
        key["components"] = nlohmann::json::array();
        for (const auto *component : path->components)
            key["components"].push_back({{"id", component->id()},
                                         {"type", component->type_name()},
                                         {"state", component->serialize()}});
    } else {
        key["path"] = nullptr;
    }
    key["generator"] = generator ? generator->serialize() : nlohmann::json(nullptr);
    return key.dump();
}
} // namespace

ReceiverPerformanceMeasurementEngine::ReceiverPerformanceMeasurementEngine(
    const NodeGraphEngine &graph, IMeasurementChainHost &host)
    : m_graph(graph), m_host(host) {}

void ReceiverPerformanceMeasurementEngine::update(
    const ReceiverRequirementsConfig &config, int point_a_pin, int point_b_pin,
    const std::vector<double> &sweep_frequencies_Hz) {
    const auto path = findMeasurementChainPath(m_graph, m_host, point_a_pin, point_b_pin);
    IComponentEngine *generator_component = nullptr;
    const int source_node = m_graph.nodeIdForPin(point_a_pin);
    if (source_node >= 0)
        generator_component = m_host.componentForNode(source_node);
    auto *generator = generator_component && generator_component->type_name() == "generator"
                          ? dynamic_cast<SignalGeneratorEngine *>(generator_component)
                          : nullptr;
    const std::string key = requestKey(config, point_a_pin, point_b_pin, sweep_frequencies_Hz,
                                       m_graph, path, generator_component);
    if (m_has_cached_request && key == m_cached_request)
        return;

    m_cached_request = key;
    m_has_cached_request = true;
    m_measurements.output_power_dBm.assign(sweep_frequencies_Hz.size(), kUnavailable);
    m_measurements.iip3_dBm.assign(sweep_frequencies_Hz.size(), kUnavailable);
    if (!path)
        return;

    IsolatedChainRunner runner(m_host);
    if (!runner.prepare(*path))
        return;

    const Spectrum::Tone *selected_tone = nullptr;
    if (generator) {
        const auto &tones = generator->tones();
        const auto selector = config.measurement_conditions.output_reference_tone_frequency_Hz;
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
    }

    const bool iip3_enabled = config.iip3_min_dBm.has_value() &&
                              config.measurement_conditions.iip3.has_value();
    const auto iip3_levels = iip3_enabled
                                 ? receiverIIP3LevelCount(*config.measurement_conditions.iip3)
                                 : std::nullopt;
    std::uint64_t stimulus_generation = 0;
    for (std::size_t i = 0; i < sweep_frequencies_Hz.size(); ++i) {
        const double center = sweep_frequencies_Hz[i];
        if (selected_tone && std::isfinite(center) && center > 0.0 &&
            std::isfinite(selected_tone->power_dBm)) {
            Spectrum stimulus;
            stimulus.generation = ++stimulus_generation;
            stimulus.tones.push_back({center, selected_tone->power_dBm, selected_tone->phase_deg});
            if (const auto output = tonePower(runner.run(stimulus), center))
                m_measurements.output_power_dBm[i] = *output;
        }

        if (!iip3_levels)
            continue;
        const auto &settings = *config.measurement_conditions.iip3;
        const double half_spacing = settings.tone_spacing_Hz / 2.0;
        const double low = center - half_spacing;
        const double high = center + half_spacing;
        if (!std::isfinite(center) || !std::isfinite(settings.tone_spacing_Hz) ||
            !(settings.tone_spacing_Hz > 0.0) || !(low > 0.0) || !std::isfinite(high))
            continue;
        std::vector<std::pair<double, double>> lower_fundamental;
        std::vector<std::pair<double, double>> upper_fundamental;
        std::vector<std::pair<double, double>> lower_im3;
        std::vector<std::pair<double, double>> upper_im3;
        lower_fundamental.reserve(*iip3_levels);
        upper_fundamental.reserve(*iip3_levels);
        lower_im3.reserve(*iip3_levels);
        upper_im3.reserve(*iip3_levels);
        for (std::size_t level = 0; level < *iip3_levels; ++level) {
            const double input_dBm = settings.input_start_dBm +
                                     static_cast<double>(level) * settings.input_step_dB;
            Spectrum stimulus;
            stimulus.generation = ++stimulus_generation;
            stimulus.tones = {{low, input_dBm, 0.0}, {high, input_dBm, 0.0}};
            const Spectrum *output = runner.run(stimulus);
            const auto low_fund = tonePower(output, low);
            const auto high_fund = tonePower(output, high);
            const auto low_im = tonePower(output, 2.0 * low - high);
            const auto high_im = tonePower(output, 2.0 * high - low);
            if (low_fund)
                lower_fundamental.emplace_back(input_dBm, *low_fund);
            if (high_fund)
                upper_fundamental.emplace_back(input_dBm, *high_fund);
            if (low_im)
                lower_im3.emplace_back(input_dBm, *low_im);
            if (high_im)
                upper_im3.emplace_back(input_dBm, *high_im);
        }
        const auto lower = estimateIIP3(lower_fundamental, lower_im3);
        const auto upper = estimateIIP3(upper_fundamental, upper_im3);
        if (lower && upper)
            m_measurements.iip3_dBm[i] = std::min(*lower, *upper);
    }
}
