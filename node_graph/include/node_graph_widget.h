#pragma once

#include "icon_registry.h"
#include "node_graph_engine.h"
#include <functional>
#include <string>

struct ImNodesEditorContext;

class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine &engine);
    ~NodeGraphWidget();

    void draw(const char *title, bool *p_open = nullptr);

    IconRegistry& iconRegistry() { return m_icons; }
    ImNodesEditorContext* context() const { return m_context; }

    // Callbacks for app to create/destroy components
    std::function<void()> onAddGenerator;
    std::function<void()> onAddAmplifier;
    std::function<void()> onAddSplitter;
    std::function<void()> onAddMixer;
    std::function<void()> onAddSParamAmp;
    std::function<void()> onAddSParamFilter;
    std::function<void()> onAddAdc;
    std::function<void()> onAddPFB;
    std::function<void(int node_id)> onRemoveNode;
    std::function<std::string(int graph_node_id)> onNodeHover;
    std::function<void()> onTopologyChange;

  private:
    NodeGraphEngine &m_engine;
    ImNodesEditorContext *m_context;
    IconRegistry m_icons;

    // Interaction state tracking
    int m_clicked_pin = -1;
    int m_clicked_node = -1;
    int m_context_menu_node = -1;
    bool m_link_created = false;
    float m_click_mouse_x = 0.0f;
    float m_click_mouse_y = 0.0f;

    void drawNodes();
    void showPinTooltips();
    void showNodeHoverTooltips();
    void drawLinks();
    void handleContextMenu(bool editor_hovered);
    void handleLinkCreation();
    void handleLinkDeletion();
    void handleNodeDeletion();
    void handleProbeClick();
};
