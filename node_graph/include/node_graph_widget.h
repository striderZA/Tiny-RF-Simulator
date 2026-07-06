#pragma once

#include "icon_registry.h"
#include "group.h"
#include "node_graph_engine.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct ImVec2;

struct ImNodesEditorContext;

class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine &engine);
    ~NodeGraphWidget();

    void draw(const char *title, bool *p_open = nullptr);

    IconRegistry& iconRegistry() { return m_icons; }

    // Callbacks for app to create/destroy components
    std::function<void()> onAddGenerator;
    std::function<void()> onAddAmplifier;
    std::function<void()> onAddSplitter;
    std::function<void()> onAddMixer;
    std::function<void()> onAddAdc;
    std::function<void()> onAddPFB;
    std::function<void()> onAddIdealFilter;
    std::function<void()> onAddCoaxCable;
    std::function<void(int node_id)> onRemoveNode;
    std::function<std::string(int graph_node_id)> onNodeHover;

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

    // Subcircuit state
    std::unordered_map<int, int> m_synth_pin_to_real_pin;  // rebuilt every frame
    std::unordered_map<int, int> m_phantom_id_for_node;    // rebuilt every frame if needed
    bool m_use_phantom_nodes = false;
    int m_context_menu_group_id = -1;
    int m_pending_rename_group_id = -1;
    char m_rename_buffer[128] = {};

    // Rubber-band state & screen-position cache
    bool m_rubber_band_active = false;
    ImVec2 m_rubber_band_start = ImVec2(0, 0);
    ImVec2 m_rubber_band_end = ImVec2(0, 0);
    std::vector<int> m_rubber_band_members;
    std::unordered_map<int, ImVec2> m_node_screen_positions;
    ImVec2 m_grid_to_screen_offset = ImVec2(0, 0);
    bool m_show_create_popup = false;

    // Internal rendering helpers
    std::unordered_map<int, int> m_real_to_synth_pin;
    void rebuildSynthMaps();
    void drawGroupBackgrounds();
    void drawPhantomNodes();
    void drawGroupCollapsedBlocks();
    void drawGroupTitleBar(Group& g, const ImVec2& top_left);

    // Theme & symbol drawing
    void setupDarkTheme();
    static void drawSchematicSymbol(ImDrawList* dl, ImVec2 center, NodeKind kind, ImU32 color);

    // Interaction handlers
    void handleRubberBand(bool editor_hovered);
    void handleGroupSelection();
    size_t findNodeIndex(int node_id) const;
};
