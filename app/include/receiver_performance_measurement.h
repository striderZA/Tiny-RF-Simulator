#pragma once

#include "receiver_requirements.h"

#include <string>
#include <vector>

class IMeasurementChainHost;
class NodeGraphEngine;

struct ReceiverPerformanceMeasurements {
    std::vector<double> output_power_dBm;
    std::vector<double> iip3_dBm;
};

class ReceiverPerformanceMeasurementEngine {
  public:
    ReceiverPerformanceMeasurementEngine(const NodeGraphEngine &graph,
                                         IMeasurementChainHost &host);

    void update(const ReceiverRequirementsConfig &config, int point_a_pin, int point_b_pin,
                const std::vector<double> &sweep_frequencies_Hz);
    const ReceiverPerformanceMeasurements &measurements() const { return m_measurements; }

  private:
    const NodeGraphEngine &m_graph;
    IMeasurementChainHost &m_host;
    ReceiverPerformanceMeasurements m_measurements;
    std::string m_cached_request;
    bool m_has_cached_request = false;
};
