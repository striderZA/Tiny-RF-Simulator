#pragma once

#include "node_graph_engine.h"
#include <functional>

class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine& engine);
    ~NodeGraphWidget();

    void draw(const char* title);

    // Callbacks for app to create/destroy components
    std::function<void()> onAddGenerator;
    std::function<void()> onAddAmplifier;
    std::function<void(int node_id)> onRemoveNode;

  private:
    NodeGraphEngine& m_engine;
    void* m_context; // ImNodesEditorContext*
    int m_hovered_pin = -1;

    void drawNodes();
    void drawLinks();
    void handleContextMenu();
    void handleLinkCreation();
    void handleLinkDeletion();
    void handleNodeDeletion();
    void handleProbeClick();
};
