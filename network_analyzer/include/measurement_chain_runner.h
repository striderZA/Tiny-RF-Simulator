#pragma once

#include "spectrum.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class IComponentEngine;
class NodeGraphEngine;

// The app implements this factory so the runner stays independent of app/UI.
class IMeasurementChainScratch {
  public:
    virtual ~IMeasurementChainScratch() = default;
    virtual IComponentEngine *createClone(std::string_view type, int id) = 0;
};

// Resolves live graph nodes and creates a fresh, disposable clone environment.
// The scratch implementation owns its graph and all clones for the pass.
class IMeasurementChainHost {
  public:
    virtual ~IMeasurementChainHost() = default;
    virtual IComponentEngine *componentForNode(int graph_node_id) const = 0;
    virtual std::unique_ptr<IMeasurementChainScratch> beginScratchPass() const = 0;
};

struct MeasurementChainPath {
    std::vector<IComponentEngine *> components;
    // Edge i connects components[i] output_ports[i] to components[i + 1]
    // input_ports[i]. Point B can select any output port on components.back().
    std::vector<int> output_ports;
    std::vector<int> input_ports;
    int point_b_output_port = 0;

    std::string signature() const;
};

std::optional<MeasurementChainPath> findMeasurementChainPath(const NodeGraphEngine &graph,
                                                             const IMeasurementChainHost &host,
                                                             int point_a_pin, int point_b_pin);

class IsolatedChainRunner {
  public:
    explicit IsolatedChainRunner(IMeasurementChainHost &host);
    bool prepare(const MeasurementChainPath &path);
    // The returned spectrum is owned by a scratch clone and is invalidated by
    // the next run() call or this runner's destruction.
    const Spectrum *run(const Spectrum &stimulus);

  private:
    IMeasurementChainHost &m_host;
    std::unique_ptr<IMeasurementChainScratch> m_scratch;
    std::vector<IComponentEngine *> m_clones;
    int m_point_b_output_port = 0;
    int m_first_input_port = 0;
    const Spectrum *m_last_result = nullptr;
};
