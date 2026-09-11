#pragma once

#include "spectrum.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct MetricSample {
    std::string name;   // metric name, e.g. "power_dBm"
    int component = -1; // IComponentEngine::id() the reading came from
    int port = 0;       // output port index
    double value = 0.0; // meaningless unless `valid`
    std::string unit;
    bool valid = false;
};

struct MetricDefinition {
    std::string unit;
    // Value of the metric for `spec`. Returns NaN when the metric is not
    // computable, and may return -inf for a well-formed but silent spectrum.
    std::function<double(const Spectrum &)> compute;
};

// Name -> metric. Adding a metric is one function in flow_metrics.cpp plus one
// registration in the constructor below.
class MetricRegistry {
  public:
    static MetricRegistry &instance();
    const MetricDefinition *find(const std::string &name) const;
    std::vector<std::string> names() const;

  private:
    MetricRegistry();
    std::unordered_map<std::string, MetricDefinition> m_defs;
};
