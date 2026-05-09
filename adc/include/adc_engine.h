#pragma once

#include <string>

#include "node_graph_engine.h"
#include "signal_node.h"
#include <nlohmann/json.hpp>

class AdcEngine {
public:
    AdcEngine(int id, NodeGraphEngine& graph);

    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;
    void serialize(nlohmann::json& j) const;
    void deserialize(const nlohmann::json& j);
    bool isDirty() const { return m_dirty; }

    void update(double dt);

    double fs_Hz() const { return m_fs_Hz; }
    void setFs_Hz(double fs) {
        if (fs != m_fs_Hz) { m_fs_Hz = fs; m_dirty = true; }
    }
    double nsd_dBm_per_Hz() const { return m_nsd_dBm_per_Hz; }
    void setNsd_dBm_per_Hz(double nsd) {
        if (nsd != m_nsd_dBm_per_Hz) { m_nsd_dBm_per_Hz = nsd; m_dirty = true; }
    }
    int bits() const { return m_bits; }
    void setBits(int b) {
        int nb = b > 0 ? b : 1;
        if (nb != m_bits) { m_bits = nb; m_dirty = true; }
    }
    double v_fs() const { return m_v_fs; }
    void setVfs(double v) {
        if (v != m_v_fs) { m_v_fs = v; m_dirty = true; }
    }

    SignalNode& node() { return m_node; }

private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph;
    SignalNode m_node;

    double m_fs_Hz = 1e9;
    double m_nsd_dBm_per_Hz = -155.0;
    int m_bits = 12;
    double m_v_fs = 2.0;
    bool m_dirty = true;
    const Spectrum* m_cached_input_ptr = nullptr;
    uint64_t m_cached_input_generation = 0;
};
