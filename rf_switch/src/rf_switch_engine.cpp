#include "rf_switch_engine.h"
#include "common.h"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

RFSwitchEngine::RFSwitchEngine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "SPDT Switch", 1, 2) {
    if (m_graph)
        m_graph->setNodePinLabels(m_graph_node_id, {"COM"}, {"T1", "T2"});
}

int RFSwitchEngine::inputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0)
        return -1;
    if (index != 0)
        return -1;
    return m_graph->inputPinId(m_graph_node_id);
}

int RFSwitchEngine::outputPinId(int index) const {
    if (!m_graph || m_graph_node_id < 0)
        return -1;
    for (const auto &node : m_graph->nodes()) {
        if (node.node_id == m_graph_node_id) {
            if (index < 0 || static_cast<size_t>(index) >= node.output_pin_ids.size())
                return -1;
            return node.output_pin_ids[index];
        }
    }
    return -1;
}

void RFSwitchEngine::setActiveThrow(int throw_index) {
    m_active_throw = std::clamp(throw_index, 0, 1);
    m_dirty = true;
}

void RFSwitchEngine::setInsertionLoss_dB(double dB) {
    m_insertion_loss_dB = std::clamp(dB, 0.0, MAX_INSERTION_LOSS_DB);
    m_dirty = true;
}

void RFSwitchEngine::setIsolation_dB(double dB) {
    m_isolation_dB = std::clamp(dB, 0.0, MAX_ISOLATION_DB);
    m_dirty = true;
}

void RFSwitchEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!beginUpdate(in_ptr))
        return;

    for (size_t out_idx = 0; out_idx < m_node.outputs.size(); ++out_idx) {
        auto &out = m_node.outputs[out_idx];
        const bool active = (static_cast<int>(out_idx) == m_active_throw);
        const double loss_dB = active ? m_insertion_loss_dB : m_isolation_dB;
        const double G = dbToLinear(-loss_dB);

        if (in_ptr && !in_ptr->frequencies.empty())
            out.frequencies = in_ptr->frequencies;
        else if (out.frequencies.size() < 2)
            buildDefaultFrequencyGrid(out.frequencies);

        const size_t N = out.frequencies.size();

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        for (auto &t : out.tones)
            t.power_dBm -= loss_dB;

        if (in_ptr && !in_ptr->phase_deg.empty())
            out.phase_deg = in_ptr->phase_deg;
        else
            out.phase_deg.assign(N, 0.0);

        if (N < 2) {
            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.assign(N, 0.0);
            out.phase_deg.assign(N, 0.0);
            out.bumpGeneration();
            continue;
        }

        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.resize(N);
        for (size_t i = 0; i < N; ++i) {
            const double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = G * nin;
            out.noise_added_W[i] = k * T * (1.0 - G);
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
        }

        out.bumpGeneration();
    }
}

nlohmann::json RFSwitchEngine::serialize() const {
    return {{"active_throw", m_active_throw},
            {"insertion_loss_dB", m_insertion_loss_dB},
            {"isolation_dB", m_isolation_dB}};
}

void RFSwitchEngine::deserialize(const nlohmann::json &j) {
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

std::string RFSwitchEngine::hoverSummary() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "SPDT: %s active | IL %.2f dB | ISO %.1f dB",
                  m_active_throw == 0 ? "T1" : "T2", m_insertion_loss_dB, m_isolation_dB);
    return buf;
}
