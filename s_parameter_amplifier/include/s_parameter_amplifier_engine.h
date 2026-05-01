#pragma once

#include "node_graph_engine.h"
#include "signal_node.h"
#include <complex>
#include <string>
#include <vector>

class SParameterAmplifierEngine {
  public:
    SParameterAmplifierEngine(int id, NodeGraphEngine& graph, const std::string& filepath);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    std::string hoverSummary() const;
    int inputPinId() const;
    int outputPinId() const;

    void update(double dt);
    void reload(const std::string& filepath);

    SignalNode& node() { return m_node; }
    const std::string& filepath() const { return m_filepath; }
    bool loaded() const { return m_loaded; }

    int numPorts() const { return m_num_ports; }
    const std::vector<double>& freqs() const { return m_freqs; }
    const std::vector<std::vector<std::complex<double>>>& params() const { return m_params; }
    int forwardParamIdx() const { return m_forward_param_idx; }
    void setForwardParamIdx(int idx);

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    std::string m_filepath;
    bool m_loaded = false;

    int m_num_ports = 0;
    std::vector<double> m_freqs;
    // [freq_idx][param_idx], param_idx = row*num_ports + col for S(p+1)(q+1)
    std::vector<std::vector<std::complex<double>>> m_params;
    int m_forward_param_idx = 0;

    std::complex<double> interpolateParam(double freq_Hz, int param_idx) const;
};
