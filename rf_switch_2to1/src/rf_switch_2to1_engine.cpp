#include "rf_switch_2to1_engine.h"
#include "common.h"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

RFSwitch2to1Engine::RFSwitch2to1Engine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "SPDT Switch (2:1)", 2, 1) {
    if (m_graph)
        m_graph->setNodePinLabels(m_graph_node_id, {"T1", "T2"}, {"COM"});
}

int RFSwitch2to1Engine::inputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0)
        return -1;
    for (const auto &node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (index < 0 || static_cast<size_t>(index) >= node.input_pin_ids.size())
                return -1;
            return node.input_pin_ids[index];
        }
    }
    return -1;
}

int RFSwitch2to1Engine::outputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0)
        return -1;
    if (index != 0)
        return -1;
    return m_graph->outputPinId(m_graph_node_id);
}

void RFSwitch2to1Engine::setActiveThrow(int throw_index) {
    m_active_throw = std::clamp(throw_index, 0, 1);
    m_dirty = true;
}

void RFSwitch2to1Engine::setInsertionLoss_dB(double dB) {
    m_insertion_loss_dB = std::clamp(dB, 0.0, MAX_INSERTION_LOSS_DB);
    m_dirty = true;
}

void RFSwitch2to1Engine::setIsolation_dB(double dB) {
    m_isolation_dB = std::clamp(dB, 0.0, MAX_ISOLATION_DB);
    m_dirty = true;
}

bool RFSwitch2to1Engine::beginUpdate2(const Spectrum *in0, const Spectrum *in1) {
    if (!m_dirty && in0 == m_cached_input0_ptr && in1 == m_cached_input1_ptr &&
        (!in0 || in0->generation == m_cached_input0_generation) &&
        (!in1 || in1->generation == m_cached_input1_generation))
        return false;
    m_dirty = false;
    m_cached_input0_ptr = in0;
    m_cached_input1_ptr = in1;
    if (in0)
        m_cached_input0_generation = in0->generation;
    if (in1)
        m_cached_input1_generation = in1->generation;
    return true;
}

void RFSwitch2to1Engine::update(double dt) {
    (void)dt;
    const Spectrum *in0 = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    const Spectrum *in1 = m_node.inputs.size() < 2 ? nullptr : m_node.inputs[1];
    if (!beginUpdate2(in0, in1))
        return;

    const Spectrum *in_sel = (m_active_throw == 0) ? in0 : in1;
    const Spectrum *in_unsel = (m_active_throw == 0) ? in1 : in0;

    auto &out = m_node.outputs[0];

    if (in_sel && !in_sel->frequencies.empty())
        out.frequencies = in_sel->frequencies;
    else if (in_unsel && !in_unsel->frequencies.empty())
        out.frequencies = in_unsel->frequencies;
    else if (out.frequencies.size() < 2)
        buildDefaultFrequencyGrid(out.frequencies);

    const size_t N = out.frequencies.size();
    const double G_IL = dbToLinear(-m_insertion_loss_dB);
    const double G_ISO = dbToLinear(-m_isolation_dB);

    // Superpose the two throw paths: the selected throw at insertion loss
    // first, then the unselected throw's leakage at the isolation floor
    // (the Combiner manual-mode convention: per-tone power adjustment, tones
    // concatenated rather than vector-summed).
    std::vector<Spectrum::Tone> tones;
    if (in_sel) {
        for (const auto &t : in_sel->tones) {
            Spectrum::Tone t_out = t;
            t_out.power_dBm -= m_insertion_loss_dB;
            tones.push_back(t_out);
        }
    }
    if (in_unsel) {
        for (const auto &t : in_unsel->tones) {
            Spectrum::Tone t_out = t;
            t_out.power_dBm -= m_isolation_dB;
            tones.push_back(t_out);
        }
    }
    out.tones = std::move(tones);

    out.is_complex_baseband =
        (in_sel && in_sel->is_complex_baseband) || (in_unsel && in_unsel->is_complex_baseband);
    out.fs_Hz = in_sel ? in_sel->fs_Hz : (in_unsel ? in_unsel->fs_Hz : 0.0);

    // The selected path is the signal path, so its phase is the output's.
    if (in_sel && !in_sel->phase_deg.empty())
        out.phase_deg = in_sel->phase_deg;
    else
        out.phase_deg.assign(N, 0.0);

    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    out.noise_W.assign(N, 0.0);
    out.noise_added_W.assign(N, 0.0);
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        const double n_sel =
            (in_sel && i < in_sel->noise_total_W.size() ? in_sel->noise_total_W[i] : 0.0);
        const double n_unsel =
            (in_unsel && i < in_unsel->noise_total_W.size() ? in_unsel->noise_total_W[i] : 0.0);
        out.noise_W[i] = G_IL * n_sel + G_ISO * n_unsel;
        out.noise_added_W[i] = k * T * std::max(0.0, 1.0 - G_IL - G_ISO);
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.bumpGeneration();
}

nlohmann::json RFSwitch2to1Engine::serialize() const {
    return {{"active_throw", m_active_throw},
            {"insertion_loss_dB", m_insertion_loss_dB},
            {"isolation_dB", m_isolation_dB}};
}

void RFSwitch2to1Engine::deserialize(const nlohmann::json &j) {
    // active_throw arrives either as the authored name ("T1"/"T2", the
    // component-library form / descriptor enum) or as the integer the project
    // serializer writes. An unrecognized name leaves the current throw alone,
    // mirroring IdealFilterEngine's filter_type handling.
    if (j.contains("active_throw")) {
        if (j["active_throw"].is_string()) {
            const std::string named = j["active_throw"].get<std::string>();
            if (named == "T1")
                setActiveThrow(0);
            else if (named == "T2")
                setActiveThrow(1);
        } else {
            setActiveThrow(j.value("active_throw", 0));
        }
    }
    setInsertionLoss_dB(j.value("insertion_loss_dB", DEFAULT_INSERTION_LOSS_DB));
    setIsolation_dB(j.value("isolation_dB", DEFAULT_ISOLATION_DB));
    m_dirty = true;
}

std::string RFSwitch2to1Engine::hoverSummary() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "SPDT 2:1: %s active | IL %.2f dB | ISO %.1f dB",
                  m_active_throw == 0 ? "T1" : "T2", m_insertion_loss_dB, m_isolation_dB);
    return buf;
}
