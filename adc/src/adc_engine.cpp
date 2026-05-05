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
    return m_graph ? m_graph->inputPinId(m_graph_node_id) : -1;
}

int AdcEngine::outputPinId() const {
    return m_graph ? m_graph->outputPinId(m_graph_node_id) : -1;
}

void AdcEngine::update(double /*dt*/) {
    const Spectrum* input = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && input == m_cached_input_ptr && (!input || input->generation == m_cached_input_generation))
        return;
    m_dirty = false;
    m_cached_input_ptr = input;
    if (input)
        m_cached_input_generation = input->generation;

    auto& out = m_node.outputs[0];

    // Copy input spectrum
    if (input) {
        out.frequencies = input->frequencies;
        out.phase_deg = input->phase_deg;
        out.noise_W = input->noise_W;
        out.noise_added_W = input->noise_added_W;
    } else {
        out.frequencies.clear();
        out.phase_deg.clear();
        out.noise_W.clear();
        out.noise_added_W.clear();
    }
    out.tones.clear();

    // 1. Add ADC NSD noise to each noise bin
    double nsd_W_per_Hz = 0.001 * std::pow(10.0, m_nsd_dBm_per_Hz / 10.0);
    if (!out.noise_W.empty()) {
        for (auto& n : out.noise_W)
            n += nsd_W_per_Hz;
    }

    // 2. Alias each tone into [0, Fs/2)
    if (input) {
        for (const auto& tone : input->tones) {
            Spectrum::Tone t = tone;
            t.freq_Hz = alias_frequency(tone.freq_Hz, m_fs_Hz);
            out.tones.push_back(t);
        }
    }

    out.computeTotalNoise();
    out.bumpGeneration();
}

std::string AdcEngine::hoverSummary() const {
    return "Fs: " + std::to_string(m_fs_Hz / 1e6) + " MHz | " + std::to_string(m_bits) + " bit | NSD: " + std::to_string(m_nsd_dBm_per_Hz) + " dBm/Hz";
}
