#include "agent_links.h"

#include "graph_link_policy.h"

std::string classifyLinkRejection(const NodeGraphEngine &graph, const IComponentEngine &source,
                                  const IComponentEngine &target, int start_pin, int end_pin) {
    if (graph.inputHasLink(end_pin))
        return "INPUT_OCCUPIED";
    if (graph.wouldCreateCycle(start_pin, end_pin))
        return "CYCLE";
    if (!graphLinkAllowed(&source, &target, start_pin, end_pin))
        return "ADC_TO_PFB_ONLY";
    return "POLICY";
}
