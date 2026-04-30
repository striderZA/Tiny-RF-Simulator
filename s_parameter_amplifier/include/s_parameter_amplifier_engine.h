#pragma once

#include "common.h"
#include "node_graph_engine.h"
#include "signal_node.h"
#include "touchstone_parser.h"
#include <string>
#include <vector>

class SParameterAmplifierEngine {
  public:
    SParameterAmplifierEngine(int id, NodeGraphEngine& graph, const std::string& filepath);
    int id() const { return m_id; }
    int graphNodeId() const { return m_graph_node_id; }
    int inputPinId() const;
    int outputPinId() const;

    void update(double dt);

    SignalNode& node() { return m_node; }
    const std::string& filepath() const { return m_filepath; }
    bool loaded() const { return m_loaded; }

  private:
    int m_id;
    int m_graph_node_id = -1;
    NodeGraphEngine* m_graph = nullptr;

    SignalNode m_node;
    std::string m_filepath;
    bool m_loaded = false;

    // S21 data: frequency (Hz) -> linear magnitude
    std::vector<double> m_s21_freqs;
    std::vector<double> m_s21_mag;

    double interpolateS21Mag(double freq_Hz) const;
};
