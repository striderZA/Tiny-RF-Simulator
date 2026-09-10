#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"

#include <span>

// Points every component input at the Spectrum produced by whatever its input
// pin is linked to, or at nullptr when nothing (or a physically disallowed
// source) is connected. The single implementation shared by
// RfSimulatorApp::rewireInputs() and the test-flow runner, so the GUI and the
// harness can never disagree about what a circuit is wired to.
void rewireComponentInputs(std::span<IComponentEngine *const> components,
                           const NodeGraphEngine &graph);
