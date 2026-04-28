#include "node_graph_widget.h"
#include "imgui.h"
#include "imnodes.h"

NodeGraphWidget::NodeGraphWidget(NodeGraphEngine &engine) : m_engine(engine), m_context(nullptr) {
    m_context = ImNodes::EditorContextCreate();
    ImNodes::EditorContextSet(m_context);
}

NodeGraphWidget::~NodeGraphWidget() { ImNodes::EditorContextFree(m_context); }

void NodeGraphWidget::draw(const char *title, bool *p_open) {
    ImNodes::EditorContextSet(m_context);

    if (ImGui::Begin(title, p_open)) {
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
    for (const auto &node : m_engine.nodes()) {
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
            ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
    }
}

void NodeGraphWidget::drawLinks() {
    for (const auto &link : m_engine.links()) {
        ImNodes::Link(link.link_id, link.start_pin_id, link.end_pin_id);
    }
}

void NodeGraphWidget::handleContextMenu() {
    if (!ImGui::IsMouseClicked(1))
        return;
    // TODO(Task 4): implement context menu
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
    // TODO(Task 4): implement node deletion
}

void NodeGraphWidget::handleProbeClick() {
    int active_attr = -1;
    if (ImNodes::IsAnyAttributeActive(&active_attr)) {
        for (const auto &node : m_engine.nodes()) {
            if (node.output_pin_id == active_attr && ImGui::IsMouseClicked(0)) {
                m_engine.setActiveProbePin(active_attr);
            }
        }
    }
}
