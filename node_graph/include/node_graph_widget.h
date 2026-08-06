#pragma once

#include "group.h"
#include "icon_registry.h"
#include "node_graph_engine.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ImVec2;

struct ImNodesEditorContext;

class NodeGraphWidget {
  public:
    NodeGraphWidget(NodeGraphEngine &engine);
    ~NodeGraphWidget();

    void draw(const char *title, bool *p_open = nullptr);

    IconRegistry &iconRegistry() { return m_icons; }

    // Callbacks for app to create/destroy components
    std::function<void()> onNodeMoved;
    std::function<void(int node_id)> onRemoveNode;
    std::function<void(int node_id)> onDuplicateNode;
    std::function<void()> onLinkChanged;
    std::function<std::string(int graph_node_id)> onNodeHover;

    // Data-driven canvas menu: app populates from ComponentTypeRegistry.
    struct AddableComponent {
        std::string menu_label;
        std::function<void(ImVec2)> on_add;
    };
    void setAddableComponents(std::vector<AddableComponent> addable) {
        m_addable_components = std::move(addable);
    }
    const std::vector<AddableComponent> &addableComponents() const { return m_addable_components; }

    ImNodesEditorContext *context() { return m_context; }
    void syncNodesFromEngine();
    void clearPositionCache() {
        m_last_node_grid_positions.clear();
        m_node_screen_positions.clear();
        m_registered_in_pool.clear();
    }
    void markNodesRegistered();
    ImVec2 gridToScreenOffset() const { return m_grid_to_screen_offset; }
    ImVec2 nodeGridPosition(int node_id) const {
        auto it = m_last_node_grid_positions.find(node_id);
        return it != m_last_node_grid_positions.end() ? it->second : ImVec2(-1, -1);
    }

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
    std::unordered_map<int, int> m_synth_pin_to_real_pin; // rebuilt every frame
    std::unordered_map<int, int>
        m_real_to_synth_pin; // rebuilt every frame (inverse, for link drawing)

    int m_context_menu_group_id = -1;
    ImVec2 m_context_menu_pos = ImVec2(0, 0); // Editor-space position of right-click
    int m_pending_rename_group_id = -1;
    char m_rename_buffer[128] = {};

    // Rubber-band state & screen-position cache
    bool m_rubber_band_active = false;
    ImVec2 m_rubber_band_start = ImVec2(0, 0);
    ImVec2 m_rubber_band_end = ImVec2(0, 0);
    std::vector<int> m_rubber_band_members;
    std::unordered_map<int, ImVec2> m_node_screen_positions;
    ImVec2 m_grid_to_screen_offset = ImVec2(0, 0);

    // Last known grid-space positions for detecting node moves
    std::unordered_map<int, ImVec2> m_last_node_grid_positions;
    // Set of node IDs registered in the ImNodes pool, so syncNodesFromEngine
    // can register new nodes without resetting existing positions.
    std::unordered_set<int> m_registered_in_pool;
    std::vector<AddableComponent> m_addable_components;
    bool m_show_create_popup = false;

    // Internal rendering helpers
    void rebuildSynthMaps();
    void drawGroupBackgrounds();
    void drawGroupCollapsedBlocks();
    void drawGroupTitleBar(Group &g, const ImVec2 &top_left);

    // Theme & symbol drawing
    void setupDarkTheme();
    static void drawSchematicSymbol(ImDrawList *dl, ImVec2 center, NodeKind kind, ImU32 color);

    // Interaction handlers
    void detectNodeMoves();
    void handleRubberBand(bool editor_hovered);
    void handleGroupSelection();
    size_t findNodeIndex(int node_id) const;
};
