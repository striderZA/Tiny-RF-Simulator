#include "coax_cable_engine.h"
#include "logging_core.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

CoaxCableEngine::CoaxCableEngine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "Coax Cable", 1, 1) {}

void CoaxCableEngine::setPresetIndex(int idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= kCoaxCablePresets.size())
        return;
    if (idx != m_preset_index) {
        m_preset_index = idx;
        m_dirty = true;
        m_warned_above_max = false; // reset warn flag on preset change
    }
}

void CoaxCableEngine::setLengthM(double m) {
    double clamped = std::clamp(m, 0.0, 1000.0);
    if (clamped != m_length_m) {
        m_length_m = clamped;
        m_dirty = true;
    }
}

void CoaxCableEngine::setConnectorsLossDB(double db) {
    double clamped = std::clamp(db, 0.0, 30.0);
    if (clamped != m_connectors_loss_dB) {
        m_connectors_loss_dB = clamped;
        m_dirty = true;
    }
}

void CoaxCableEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!beginUpdate(in_ptr))
        return;

    auto &out = m_node.outputs[0];

    // Pass-through: copy frequencies, tones, phase, noise from input. The
    // frequency-dependent loss and phase shift are added in Task 3+.
    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;

    const CableSpec &p = preset();
    const double max_f_Hz = p.max_freq_GHz * 1e9;
    for (auto &t : out.tones) {
        const double f_Hz_raw = std::abs(t.freq_Hz);
        const double f_Hz = std::clamp(f_Hz_raw, 1.0, max_f_Hz);
        if (f_Hz_raw > max_f_Hz && !m_warned_above_max) {
            LOG_WARN(
                "Coax cable %d: tone at %.3e Hz exceeds preset %s max freq (%.3e Hz); clamping.",
                m_id, f_Hz_raw, p.name, max_f_Hz);
            m_warned_above_max = true;
        }
        const double f_MHz = f_Hz / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(f_MHz) + p.K2_dB_per_m * f_MHz) * m_length_m +
            m_connectors_loss_dB;
        t.power_dBm -= loss_dB;
        const double phase_shift_deg = -360.0 * (f_Hz / 1e9) * m_length_m * p.delay_ns_per_m;
        t.phase_deg += phase_shift_deg;
    }

    const size_t N = out.frequencies.size();
    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }
    for (size_t i = 0; i < N; ++i) {
        const double f_Hz = std::abs(out.frequencies[i]);
        const double f_Hz_c = std::clamp(f_Hz, 1.0, p.max_freq_GHz * 1e9);
        const double phase_shift_deg = -360.0 * (f_Hz_c / 1e9) * m_length_m * p.delay_ns_per_m;
        out.phase_deg[i] += phase_shift_deg;
    }

    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        const double f_Hz = std::abs(out.frequencies[i]);
        const double f_Hz_c = std::clamp(f_Hz, 1.0, p.max_freq_GHz * 1e9);
        const double f_MHz = f_Hz_c / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(f_MHz) + p.K2_dB_per_m * f_MHz) * m_length_m +
            m_connectors_loss_dB;
        const double L_lin = dbToLinear(loss_dB);
        const double nin =
            (in_ptr && i < in_ptr->noise_total_W.size()) ? in_ptr->noise_total_W[i] : 0.0;
        out.noise_W[i] = nin / L_lin;
        out.noise_total_W[i] = out.noise_W[i]; // noise_added_W is 0
    }

    out.bumpGeneration();
}

nlohmann::json CoaxCableEngine::serialize() const {
    return {{"preset_index", m_preset_index},
            {"length_m", m_length_m},
            {"connectors_loss_dB", m_connectors_loss_dB}};
}

void CoaxCableEngine::deserialize(const nlohmann::json &j) {
    // Clamp to the same ranges the setters enforce: a corrupted/hand-edited
    // .rfsim with an out-of-range preset index must not reach preset()'s
    // kCoaxCablePresets[] indexing (OOB/UB).
    m_preset_index =
        std::clamp(j.value("preset_index", 4), 0, static_cast<int>(kCoaxCablePresets.size()) - 1);
    m_length_m = std::clamp(j.value("length_m", 1.0), 0.0, 1000.0);
    m_connectors_loss_dB = std::clamp(j.value("connectors_loss_dB", 0.0), 0.0, 30.0);
    m_dirty = true;
}

std::string CoaxCableEngine::hoverSummary() const {
    std::string summary =
        "Coax Cable: " + std::string(preset().name) + " | L=" + std::to_string(m_length_m) + " m";

    if (!m_node.inputs.empty() && m_node.inputs[0] && !m_node.inputs[0]->frequencies.empty()) {
        const auto &freqs = m_node.inputs[0]->frequencies;
        const double fc = std::abs((freqs.front() + freqs.back()) / 2.0);
        const double fc_clamped = std::clamp(fc, 1.0, preset().max_freq_GHz * 1e9);
        const double fc_MHz = fc_clamped / 1e6;
        const double loss_dB =
            (preset().K1_dB_per_m * std::sqrt(fc_MHz) + preset().K2_dB_per_m * fc_MHz) *
                m_length_m +
            m_connectors_loss_dB;
        summary += " | Loss@fc=" + std::to_string(loss_dB) + " dB";
    }

    return summary;
}
