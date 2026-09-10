#include "flow_metrics.h"
#include "power_meter_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

double nan() { return std::numeric_limits<double>::quiet_NaN(); }

// Total power in dBm — the same measurement the GUI power meter reports.
double totalPower_dBm(const Spectrum &spec) {
    const PowerMeasurement measurement = PowerMeterEngine{}.measure(&spec);
    return measurement.valid ? measurement.power_dBm : nan();
}

const Spectrum::Tone *strongestTone(const Spectrum &spec) {
    const Spectrum::Tone *best = nullptr;
    for (const auto &tone : spec.tones) {
        if (!best || tone.power_dBm > best->power_dBm)
            best = &tone;
    }
    return best;
}

double peakPower_dBm(const Spectrum &spec) {
    const Spectrum::Tone *tone = strongestTone(spec);
    return tone ? tone->power_dBm : nan();
}

double peakFreq_Hz(const Spectrum &spec) {
    const Spectrum::Tone *tone = strongestTone(spec);
    return tone ? tone->freq_Hz : nan();
}

// noise_total_W is a per-Hz density (W/Hz) — PowerMeterEngine integrates it as
// density * bin_width — so the mean over the grid converts to dBm/Hz directly.
double noiseFloor_dBm_per_Hz(const Spectrum &spec) {
    if (spec.frequencies.size() < 2 || spec.noise_total_W.empty())
        return nan();
    // A density vector that does not match the grid is malformed data. Truncating
    // would report a plausible value where power_dBm correctly reports
    // not-computable, so both metrics must agree the input is unmeasurable.
    if (spec.noise_total_W.size() != spec.frequencies.size())
        return nan();

    double sum = 0.0;
    for (double density : spec.noise_total_W) {
        // Per-bin, not mean-based: PowerMeterEngine rejects any negative density, so a
        // mean test would let a negative bin cancel a positive one and disagree with
        // power_dBm about whether the input is measurable at all.
        if (!std::isfinite(density) || density < 0.0)
            return nan(); // malformed data
        sum += density;
    }
    const double mean_density = sum / static_cast<double>(spec.noise_total_W.size());

    if (mean_density == 0.0)
        return -std::numeric_limits<double>::infinity(); // well-formed but silent
    return 10.0 * std::log10(mean_density * 1000.0);     // W/Hz -> dBm/Hz
}

} // namespace

MetricRegistry &MetricRegistry::instance() {
    static MetricRegistry registry;
    return registry;
}

MetricRegistry::MetricRegistry() {
    m_defs.emplace("power_dBm", MetricDefinition{"dBm", totalPower_dBm});
    m_defs.emplace("peak_power_dBm", MetricDefinition{"dBm", peakPower_dBm});
    m_defs.emplace("peak_freq_Hz", MetricDefinition{"Hz", peakFreq_Hz});
    m_defs.emplace("noise_floor_dBm_per_Hz", MetricDefinition{"dBm/Hz", noiseFloor_dBm_per_Hz});
}

const MetricDefinition *MetricRegistry::find(const std::string &name) const {
    const auto it = m_defs.find(name);
    return it == m_defs.end() ? nullptr : &it->second;
}

std::vector<std::string> MetricRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(m_defs.size());
    for (const auto &entry : m_defs)
        out.push_back(entry.first);
    std::sort(out.begin(), out.end());
    return out;
}
