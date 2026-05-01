#include "adc_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "common.h"
#include "logging_core.h"

// ---- Nyquist zone utilities ----

static double alias_frequency(double f_RF, double Fs) {
    double f = std::fmod(f_RF, Fs);
    if (f > Fs / 2.0)
        f = Fs - f;
    return f;
}

static int nyquist_zone(double f_RF, double Fs) {
    return static_cast<int>(f_RF / (Fs / 2.0)) + 1;
}

static bool is_inverted(double f_RF, double Fs) {
    return nyquist_zone(f_RF, Fs) % 2 == 0;
}

// ---- Engine methods ----

AdcEngine::AdcEngine(int id, NodeGraphEngine& graph)
    : m_id(id), m_graph(&graph) {
    m_graph_node_id = m_graph->addNode("ADC " + std::to_string(id), &m_node, 1, 1);
    m_node.inputs.resize(1);
    m_node.outputs.resize(1);
    LOG_INFO("ADC [adc%d] added.", id);
}

int AdcEngine::inputPinId() const {
    for (const auto& n : m_graph->nodes()) {
        if (n.node_id == m_graph_node_id && !n.input_pin_ids.empty())
            return n.input_pin_ids[0];
    }
    return -1;
}

int AdcEngine::outputPinId() const {
    for (const auto& n : m_graph->nodes()) {
        if (n.node_id == m_graph_node_id && !n.output_pin_ids.empty())
            return n.output_pin_ids[0];
    }
    return -1;
}

void AdcEngine::update(double /*dt*/) {
    const auto& input = m_node.inputs[0];
    auto& out = m_node.outputs[0];

    // Copy input spectrum
    out.frequencies = input.frequencies;
    out.phase_deg = input.phase_deg;
    out.noise_W = input.noise_W;
    out.noise_added_W = input.noise_added_W;
    out.tones = input.tones;
    // Clear tones — we rebuild them with aliased frequencies below
    out.tones.clear();

    // 1. Add ADC NSD noise to each noise bin
    double nsd_W_per_Hz = 0.001 * std::pow(10.0, m_nsd_dBm_per_Hz / 10.0);
    if (!out.noise_W.empty()) {
        for (auto& n : out.noise_W)
            n += nsd_W_per_Hz;
    }

    // 2. Frequency-translate tones: alias → shift by f_dig_chan
    double f_dig_chan = alias_frequency(m_f_channel_Hz, m_fs_Hz);
    bool inverted = is_inverted(m_f_channel_Hz, m_fs_Hz);

    for (const auto& tone : input.tones) {
        Spectrum::Tone t = tone;
        double f_alias = alias_frequency(tone.freq_Hz, m_fs_Hz);
        if (inverted)
            t.freq_Hz = -(f_alias - f_dig_chan);
        else
            t.freq_Hz = f_alias - f_dig_chan;
        out.tones.push_back(t);
    }

    // 3. Frequency-translate the noise grid: shift all frequencies by -f_dig_chan
    //    and wrap any that fall outside MIN_FREQ/MAX_FREQ
    if (!out.frequencies.empty()) {
        for (auto& f : out.frequencies)
            f -= f_dig_chan;
    }

    out.computeTotalNoise();
}
