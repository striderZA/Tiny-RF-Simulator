#include "mixer_engine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <nlohmann/json.hpp>
#include <numbers>
#include <vector>

// Sums tones that share a frequency as phasors, so coincident images add coherently. Frequencies
// are compared within a relative tolerance of each candidate's own magnitude: tone ± LO rounds
// separately per term, so equal results can differ by a few ulps. First-seen order is kept. A
// sum that cancels to rounding noise (|sum| <= 1e-12 of the summed magnitudes) is dropped.
static std::vector<Spectrum::Tone>
sumCoincidentTonesCoherently(const std::vector<Spectrum::Tone> &tones) {
    std::map<double, size_t> slot_by_freq;
    std::vector<double> freqs;
    std::vector<std::complex<double>> sums;
    std::vector<double> magnitude_sums;
    for (const auto &t : tones) {
        const double amp = std::sqrt(0.001 * std::pow(10.0, t.power_dBm / 10.0));
        const double phase_rad = t.phase_deg * std::numbers::pi / 180.0;
        const double tol_Hz = std::abs(t.freq_Hz) * 1e-12;
        const auto it = slot_by_freq.lower_bound(t.freq_Hz - tol_Hz);
        size_t idx = freqs.size();
        if (it != slot_by_freq.end() && it->first <= t.freq_Hz + tol_Hz) {
            idx = it->second;
        } else {
            freqs.push_back(t.freq_Hz);
            sums.emplace_back(0.0, 0.0);
            magnitude_sums.push_back(0.0);
            slot_by_freq.emplace(t.freq_Hz, idx);
        }
        sums[idx] += std::polar(amp, phase_rad);
        magnitude_sums[idx] += amp;
    }

    std::vector<Spectrum::Tone> result;
    result.reserve(freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i) {
        if (std::abs(sums[i]) <= 1e-12 * magnitude_sums[i])
            continue;
        const double power_W = std::norm(sums[i]);
        result.push_back({freqs[i], 10.0 * std::log10(power_W / 0.001),
                          std::atan2(sums[i].imag(), sums[i].real()) * 180.0 / std::numbers::pi});
    }
    return result;
}

MixerEngine::MixerEngine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "Mixer", 1, 1) {}

void MixerEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!beginUpdate(in_ptr))
        return;

    auto &out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    const size_t N = out.frequencies.size();

    // Frequency conversion: each input tone produces sum and difference
    out.tones.clear();
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    if (in_ptr) {
        std::vector<Spectrum::Tone> sidebands;
        sidebands.reserve(2 * in_ptr->tones.size());
        for (const auto &tone : in_ptr->tones) {
            Spectrum::Tone lower;
            lower.freq_Hz = in_ptr->is_complex_baseband ? (tone.freq_Hz - m_lo_freq_Hz)
                                                        : std::abs(tone.freq_Hz - m_lo_freq_Hz);
            lower.power_dBm = tone.power_dBm + m_conv_gain_dB;
            // Real A·cos(2πft+φ)·cos(2π·LO·t): the difference term cos(2π(f−LO)t+φ)
            // has phase −φ when f < LO. Complex input shifts without conjugation.
            const bool conjugates = !in_ptr->is_complex_baseband && tone.freq_Hz < m_lo_freq_Hz;
            lower.phase_deg = conjugates ? -tone.phase_deg : tone.phase_deg;
            sidebands.push_back(lower);

            Spectrum::Tone upper;
            upper.freq_Hz = tone.freq_Hz + m_lo_freq_Hz;
            upper.power_dBm = tone.power_dBm + m_conv_gain_dB;
            upper.phase_deg = tone.phase_deg;
            sidebands.push_back(upper);
        }
        out.tones = sumCoincidentTonesCoherently(sidebands);
    }

    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    double G = dbToLinear(m_conv_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    if (in_ptr) {
        const auto addTranslatedNoise = [&](double frequency_Hz, double density_W_per_Hz) {
            const auto bin =
                std::lower_bound(out.frequencies.begin(), out.frequencies.end(), frequency_Hz);
            if (bin == out.frequencies.end() || frequency_Hz < out.frequencies.front())
                return;

            size_t index = static_cast<size_t>(bin - out.frequencies.begin());
            if (index > 0 &&
                frequency_Hz - out.frequencies[index - 1] <= out.frequencies[index] - frequency_Hz)
                --index;
            out.noise_W[index] += density_W_per_Hz;
        };

        for (size_t i = 0; i < in_ptr->frequencies.size() && i < in_ptr->noise_total_W.size();
             ++i) {
            const double density_W_per_Hz = G * in_ptr->noise_total_W[i];
            const double input_frequency_Hz = in_ptr->frequencies[i];
            const double difference_frequency_Hz =
                in_ptr->is_complex_baseband ? input_frequency_Hz - m_lo_freq_Hz
                                            : std::abs(input_frequency_Hz - m_lo_freq_Hz);
            addTranslatedNoise(difference_frequency_Hz, density_W_per_Hz);
            addTranslatedNoise(input_frequency_Hz + m_lo_freq_Hz, density_W_per_Hz);
        }
    }

    out.noise_added_W.resize(N);
    if (added_density <= 0.0) {
        out.noise_added_W.assign(N, 0.0);
    } else {
        out.noise_added_W.assign(N, added_density);
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.bumpGeneration();
}

nlohmann::json MixerEngine::serialize() const {
    return {{"lo_freq_Hz", m_lo_freq_Hz}, {"conv_gain_dB", m_conv_gain_dB}, {"nf_dB", m_nf_dB}};
}

void MixerEngine::deserialize(const nlohmann::json &j) {
    m_lo_freq_Hz = j.value("lo_freq_Hz", 1e9);
    m_conv_gain_dB = j.contains("conv_gain_dB") ? j["conv_gain_dB"].get<double>()
                                                : j.value("conversion_gain_dB", -6.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_dirty = true;
}

std::string MixerEngine::hoverSummary() const {
    return "LO: " + std::to_string(m_lo_freq_Hz / 1e6) +
           " MHz | Conv Gain: " + std::to_string(m_conv_gain_dB) +
           " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
