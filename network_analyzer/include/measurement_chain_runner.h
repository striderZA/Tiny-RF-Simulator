#pragma once

#include "spectrum.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// The measured circuit: every component on a forward path from Point A's
// component to Point B's component, including parallel branches that rejoin
// at an RF SPDT 2:1 switch (a switched filter bank). components[0] is Point
// A's own component, never cloned because the stimulus replaces its output;
// components[1..] are cloned and run in this topological order, ending at
// Point B's component. Each edge feeds input port `input_port` of
// components[to] from output port `output_port` of components[from]; an edge
// from components[0] carries the stimulus. Point B can select any output port
// on components.back().
struct MeasurementChainPath {
    struct Edge {
        std::size_t from = 0;
        int output_port = 0;
        std::size_t to = 0;
        int input_port = 0;
    };
    std::vector<IComponentEngine *> components;
    std::vector<Edge> edges; // sorted by (to, input_port, from, output_port)
    int point_b_output_port = 0;

    std::string signature() const;
};

// Returns nullopt when Point B is unreachable from Point A or the circuit
// cannot be reproduced by one feed-forward clone pass: it has a cycle or an
// unregistered component; an input is fed from outside the circuit (e.g. a
// second live source); one input pin has two links; a component other than
// Point A's has several inputs and is not an RF SPDT 2:1 switch (a combiner
// merges paths with semantics the analyzer does not define); or Point A's
// component feeds the circuit through more than one output, where the shared
// stimulus would ignore how that component divides its signal (e.g. the
// throw of a 1:2 switch).
std::optional<MeasurementChainPath> findMeasurementChainPath(const NodeGraphEngine &graph,
                                                             const IMeasurementChainHost &host,
                                                             int point_a_pin, int point_b_pin);

class IsolatedChainRunner {
  public:
    explicit IsolatedChainRunner(IMeasurementChainHost &host);
    bool prepare(const MeasurementChainPath &path);
    // Feeds the stimulus to every input linked from Point A's component and
    // runs the clones in order. The returned spectrum is owned by a scratch
    // clone and is invalidated by the next run() call or this runner's
    // destruction.
    const Spectrum *run(const Spectrum &stimulus);

  private:
    IMeasurementChainHost &m_host;
    std::unique_ptr<IMeasurementChainScratch> m_scratch;
    // Mirrors path.components; m_clones[0] stays null because Point A's
    // component is never cloned.
    std::vector<IComponentEngine *> m_clones;
    // (clone index, input port) of every input the stimulus feeds.
    std::vector<std::pair<std::size_t, std::size_t>> m_stimulus_inputs;
    int m_point_b_output_port = 0;
    const Spectrum *m_last_result = nullptr;
};
