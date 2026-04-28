#pragma once

#include "node_graph_engine.h"
#include <functional>

struct ImNodesEditorContext;

class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine &engine);
    ~NodeGraphWidget();

    void draw(const char *title, bool *p_open = nullptr);

    // Callbacks for app to create/destroy components
    std::function<void()> onAddGenerator;
    std::function<void()> onAddAmplifier;
    std::function<void(int node_id)> onRemoveNode;

  private:
    NodeGraphEngine &m_engine;
    ImNodesEditorContext *m_context;

    void drawNodes();
    void drawLinks();
    void handleContextMenu();
    void handleLinkCreation();
    void handleLinkDeletion();
    void handleNodeDeletion();
    void handleProbeClick();
};
