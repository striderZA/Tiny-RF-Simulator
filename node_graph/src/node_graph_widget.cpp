#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"

NodeGraphWidget::NodeGraphWidget(NodeGraphEngine& engine)
    : m_engine(engine), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(static_cast<ImNodesEditorContext*>(m_context));
}

NodeGraphWidget::~NodeGraphWidget() {
    ImNodes::EditorContextFree(static_cast<ImNodesEditorContext*>(m_context));
}

void NodeGraphWidget::draw(const char* title) {
    ImNodes::EditorContextSet(static_cast<ImNodesEditorContext*>(m_context));

    if (ImGui::Begin(title)) {
        ImNodes::BeginNodeEditor();

        drawNodes();
        drawLinks();
        handleLinkCreation();
        handleLinkDeletion();
        handleNodeDeletion();
        handleProbeClick();

        ImNodes::EndNodeEditor();
        handleContextMenu();
    }
    ImGui::End();
}

void NodeGraphWidget::drawNodes() {
    m_hovered_pin = -1;
    for (const auto& node : m_engine.nodes()) {
        ImNodes::BeginNode(node.node_id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.label.c_str());
        ImNodes::EndNodeTitleBar();

        if (node.input_pin_id >= 0) {
            ImNodes::BeginInputAttribute(node.input_pin_id);
            ImGui::Text("IN");
            ImNodes::EndInputAttribute();
        }

        if (node.output_pin_id >= 0) {
            ImNodes::BeginOutputAttribute(node.output_pin_id);
            ImGui::Text("OUT");
            if (ImGui::IsItemHovered()) {
                m_hovered_pin = node.output_pin_id;
            }
            ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawLinks() {
    for (const auto& link : m_engine.links()) {
        ImNodes::Link(link.link_id, link.start_pin_id, link.end_pin_id);
    }
}

void NodeGraphWidget::handleContextMenu() {
    if (!ImGui::IsMouseClicked(1)) return;
    // Stub - will be implemented in Task 4
}

void NodeGraphWidget::handleLinkCreation() {
    int start_pin, end_pin;
    if (ImNodes::IsLinkCreated(&start_pin, &end_pin)) {
        m_engine.addLink(start_pin, end_pin);
    }
}

void NodeGraphWidget::handleLinkDeletion() {
    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id)) {
        m_engine.removeLink(link_id);
    }
}

void NodeGraphWidget::handleNodeDeletion() {
    // Stub - will be implemented in Task 4
}

void NodeGraphWidget::handleProbeClick() {
    if (m_hovered_pin >= 0 && ImGui::IsMouseClicked(0)) {
        m_engine.setActiveProbePin(m_hovered_pin);
    }
}
