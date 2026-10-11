#pragma once

#include "component_interface.h"
#include "node_graph_engine.h"

#include <string>

std::string classifyLinkRejection(const NodeGraphEngine &graph, const IComponentEngine &source,
                                  const IComponentEngine &target, int start_pin, int end_pin);
