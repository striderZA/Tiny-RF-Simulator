#pragma once

#include "group.h"
#include "imgui.h"
#include "node_graph_engine.h"
#include "node_layout.h"
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ImVec2;

struct ImNodesEditorContext;

// What the widget needs to describe a hovered node in its body tooltip: the
// owning component's summary line, plus the analyzer-measured strongest-tone
// SNR of the node's first output when that measurement is available. The widget
// renders the number but never computes it — the analyzer settings live in the
// app, which owns the callback that fills this in.
struct NodeHoverInfo {
    std::string summary;
    std::optional<double> snr_dB;
};

struct NodeGraphBounds {
    ImVec2 min;
    ImVec2 max;
};

struct NodeGraphWidgetActions {
    std::function<std::optional<int>(int, int)> connectLink;
    std::function<bool(int)> disconnectLink;
    std::function<bool(int)> removeComponent;
    std::function<bool(int)> duplicateComponent;
    std::function<bool(int)> addProbePin;
    std::function<bool(int)> removeProbePin;
    std::function<int(std::string, std::vector<int>)> createGroup;
    std::function<bool(int)> removeGroup;
    std::function<bool(int, std::string)> renameGroup;
    std::function<bool(int, bool)> setGroupCollapsed;
    std::function<void(int)> selectGroup;
    // Align and distribute. moveNodes places the listed nodes and returns the refusal reason, or an
    // empty string when accepted.
    std::function<std::string(const std::vector<LayoutMove> &)> moveNodes;
    // One undo record for a multi-item canvas delete, which is not a single command.
    std::function<void(std::string)> beginEditGroup;
    std::function<bool()> endEditGroup;
};

class NodeGraphWidget {
  public:
    NodeGraphWidget(const NodeGraphEngine &engine, NodeGraphWidgetActions actions);
    ~NodeGraphWidget();

    void draw(const char *title, bool *p_open = nullptr);

    // A short-lived message painted at the canvas's bottom-left corner, e.g. the notice after a
    // removal. It lives for four seconds (ES-4) and never takes part in layout or input.
    void showNotice(std::string text, double now);
    // The notice text to show at `now`, or empty once the notice has expired.
    std::string_view noticeAt(double now) const;

    std::function<void()> onNodeMoved;
    std::function<NodeHoverInfo(int graph_node_id)> onNodeHover;

    // Data-driven canvas menu: app populates from ComponentTypeRegistry.
    struct AddableComponent {
        std::string menu_label;
        std::function<void(ImVec2)> on_add;
    };
    void setAddableComponents(std::vector<AddableComponent> addable) {
        m_addable_components = std::move(addable);
    }
    const std::vector<AddableComponent> &addableComponents() const { return m_addable_components; }

    // Data-driven label-prefix → NodeKind mapping. The app feeds this from
    // ComponentTypeRegistry (label_prefix + kind) at startup.
    void registerNodeKind(std::string label_prefix, NodeKind kind) {
        m_kind_prefixes.push_back({std::move(label_prefix), kind});
    }
    NodeKind kindForLabel(const std::string &label) const;

    // Place a node in editor-space coordinates and keep the widget's position
    // caches current. Returns true only when its stored grid origin changes.
    bool setNodeEditorSpacePosition(int node_id, ImVec2 position);
    // Makes `node_id` the only selected node, which is what the Properties panel reads. The node
    // must already be in the editor pool, as setNodeEditorSpacePosition() guarantees.
    void selectNode(int node_id);
    // Aligns or distributes the selected nodes by their editor-space positions and sizes. A
    // selection that includes a collapsed subcircuit or its members is refused, and the refusal
    // text is left in layoutMessage() until the user dismisses the message.
    void alignSelection(AlignMode mode);
    void distributeSelection(DistributeAxis axis);
    const std::string &layoutMessage() const { return m_layout_message; }
    // Number of selection entries a layout would move: selected component nodes plus collapsed
    // blocks, with stale ImNodes entries left out. The Align and Distribute entries need two.
    int layoutSelectionCount() const;
    // Snaps the selected nodes onto the grid (ImNodes::SnapNodeToGrid), whether or not drag
    // snapping is on. The refusal rules are those of align. Nodes not yet in the pool are skipped.
    void snapSelectionToGrid();
    // Drag snapping to ImNodes' grid (TRS-80). Off by default; kept for the session only.
    void setGridSnapping(bool enabled) { m_grid_snapping = enabled; }
    bool gridSnapping() const { return m_grid_snapping; }
    // Bounds of existing component nodes and currently rendered collapsed groups in editor space,
    // excluding IDs added by the current batch. Nodes without a cached position are at the origin.
    std::optional<NodeGraphBounds>
    nodeBoundsExcluding(const std::unordered_set<int> &excluded_node_ids) const;
    ImNodesEditorContext *context() { return m_context; }
    void syncNodesFromEngine();
    void clearPositionCache() {
        m_last_node_grid_positions.clear();
        m_cached_node_dimensions.clear();
        m_cached_collapsed_group_bounds.clear();
        m_prev_node_grid_positions.clear();
        m_cached_grid_positions.clear();
        m_node_screen_positions.clear();
        m_registered_in_pool.clear();
        m_rendered_collapsed_groups.clear();
        m_hidden_last_frame.clear();
    }
    // Snapshots the current grid-space position of every engine node into the
    // cache. Call only while all engine nodes are registered in the imnodes pool
    // (e.g. right after a project load restores positions) so a collapsed
    // group's members keep their positions on the first frame where they are
    // hidden.
    void captureGridPositions();
    // True when the collapsed block for `group_id` was rendered this frame.
    bool collapsedGroupBlockRendered(int group_id) const {
        return m_rendered_collapsed_groups.count(group_id) > 0;
    }
    // Number of links drawn last frame whose two endpoints sat in different
    // collapsed groups (issue #116).
    int crossGroupLinksDrawn() const { return m_cross_group_links_drawn; }
    // Call only after each engine node already has an ImNodes pool entry, as in project restore.
    void markNodesRegistered();
    ImVec2 gridToScreenOffset() const { return m_grid_to_screen_offset; }
    // Historical name: returns the cached editor-space position.
    ImVec2 nodeGridPosition(int node_id) const {
        auto it = m_last_node_grid_positions.find(node_id);
        return it != m_last_node_grid_positions.end() ? it->second : ImVec2(-1, -1);
    }
    // Project-save, checkpoint, and agent reads use editor-space coordinates. Nodes with an
    // initialized pool position are read directly; unregistered nodes and collapsed-group
    // members use the cached grid position (or the origin) plus the current pan.
    ImVec2 editorPositionForSave(int node_id);
    // Pan-independent grid position, for undo records: editorPositionForSave() minus the pan.
    ImVec2 gridPositionForSave(int node_id);
    // Places a node at a grid position, applying the current pan so undo is pan-independent.
    void setNodeGridPosition(int node_id, ImVec2 grid_position);

    // Draws the schematic glyph for `kind` (and its label, if any) centred on `center`, inside the
    // shared 48x32 design box. Public so the geometry tests can measure the drawn vertices.
    static void drawSchematicSymbol(ImDrawList *dl, ImVec2 center, NodeKind kind, ImU32 color);

  private:
    const NodeGraphEngine &m_engine;
    NodeGraphWidgetActions m_actions;
    ImNodesEditorContext *m_context;

    // Interaction state tracking
    int m_clicked_pin = -1;
    int m_clicked_node = -1;
    int m_context_menu_node = -1;
    int m_context_menu_link_id = -1;
    bool m_link_created = false;
    float m_click_mouse_x = 0.0f;
    float m_click_mouse_y = 0.0f;

    void drawNodes();
    void showPinTooltips();
    void showNodeHoverTooltips();
    void showLinkTooltips();
    void drawLinks();
    void handleContextMenu(bool editor_hovered);
    void handleLinkCreation();
    void handleLinkDeletion();
    void handleNodeDeletion();
    // Undo label for one Delete press: the node's label, or the item count for several.
    std::string deletionLabel(int num_selected_links, int num_selected_nodes) const;
    void handleProbeClick();
    // Link mutation and boundary refresh are delegated to the action bundle.
    void removeLink(int link_id);

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
    // Collapsed-group members as of the last draw. ImNodes dropped them at that frame's
    // EndNodeEditor, so their positions come from the grid cache rather than from ImNodes.
    std::unordered_set<int> m_hidden_last_frame;
    ImVec2 m_grid_to_screen_offset = ImVec2(0, 0);

    // Last known grid-space (pan-independent) position of every node the
    // widget has seen rendered. Collapsed-group members stop being drawn, so
    // this cache is what lets drawGroupCollapsedBlocks() still place the block
    // (issue #116).
    std::unordered_map<int, ImVec2> m_cached_grid_positions;
    // Last measured ImNodes dimensions; hidden or not-yet-rendered nodes use a safe fallback.
    std::unordered_map<int, ImVec2> m_cached_node_dimensions;
    // Actual editor-space bounds of collapsed synthetic group nodes rendered by the widget.
    std::unordered_map<int, NodeGraphBounds> m_cached_collapsed_group_bounds;
    // Collapsed groups whose block was actually rendered this frame.
    std::unordered_set<int> m_rendered_collapsed_groups;
    // Links drawn last frame between two different collapsed groups.
    int m_cross_group_links_drawn = 0;

    // Externally readable cache of each node's editor-space position, updated
    // unconditionally every frame for every drawn node.
    std::unordered_map<int, ImVec2> m_last_node_grid_positions;
    // Previous-frame snapshot for detecting node moves (requires mouse release).
    std::unordered_map<int, ImVec2> m_prev_node_grid_positions;
    // Positions initialized by setters, project restore, or BeginNode prevent
    // syncNodesFromEngine() from resetting known positions; hidden nodes may leave the pool.
    std::unordered_set<int> m_registered_in_pool;
    std::vector<AddableComponent> m_addable_components;
    std::vector<std::pair<std::string, NodeKind>> m_kind_prefixes;
    bool m_show_create_popup = false;
    std::string m_layout_message;
    bool m_open_layout_message = false;
    // Collapsed subcircuits that had a selected member, as seen at the start of each draw. Names
    // the subcircuit when a collapse has pruned that member from the ImNodes selection.
    std::set<int> m_collapsed_selection_groups;
    // Drag snapping to the grid, applied to ImNodes' style at the start of each draw.
    bool m_grid_snapping = false;

    // The active notice (text plus the ImGui time it was shown at). Painted as draw-list text,
    // so it is never an item.
    std::string m_notice_text;
    double m_notice_shown_at = 0.0;
    void drawNotice();

    // Internal rendering helpers
    void rebuildSynthMaps();
    void drawGroupBackgrounds();
    void drawGroupCollapsedBlocks();
    void drawGroupTitleBar(const Group &g, const ImVec2 &top_left);

    // Theme & symbol drawing
    void setupDarkTheme();

    // Interaction handlers
    void detectNodeMoves();
    void handleRubberBand(bool editor_hovered);
    void handleGroupSelection();
    void runLayout(
        const std::function<std::vector<LayoutMove>(const std::vector<LayoutItem> &)> &layout);
    void showLayoutMessage(std::string message);
    std::vector<int> liveSelection(bool *has_stale) const;
    bool isLiveSelectionEntry(int id) const;
    void rememberCollapsedSelection();
    std::string layoutRefusal(const std::vector<int> &live, bool has_stale) const;
    // Measured node size from the last draw, or the safe fallback for a node not yet drawn.
    ImVec2 cachedNodeDimensions(int node_id) const;
};
