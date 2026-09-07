#include "power_meter_widget.h"
#include "imgui.h"
#include "node_graph_engine.h"
#include <cmath>
#include <string>
#include <vector>

PowerMeterWidget::PowerMeterWidget(PowerMeterEngine &engine, NodeGraphEngine &graph)
    : m_engine(engine), m_graph(graph) {}

void PowerMeterWidget::draw(const char *title, bool *p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    struct PinEntry {
        int pin_id;
        std::string label;
        const Spectrum *spectrum;
    };
    std::vector<PinEntry> pins;
    for (const auto &node : m_graph.nodes()) {
        if (!node.signal_node)
            continue;
        for (size_t i = 0; i < node.output_pin_ids.size(); ++i) {
            const Spectrum *spectrum =
                i < node.signal_node->outputs.size() ? &node.signal_node->outputs[i] : nullptr;
            std::string label = node.label + " OUT";
            if (i > 0)
                label += std::to_string(i + 1);
            pins.push_back({node.output_pin_ids[i], std::move(label), spectrum});
        }
    }

    std::vector<const char *> items;
    items.reserve(pins.size() + 1);
    items.push_back("(none)");
    for (const auto &pin : pins)
        items.push_back(pin.label.c_str());

    int source_index = 0;
    for (size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].pin_id == m_source_pin) {
            source_index = static_cast<int>(i) + 1;
            break;
        }
    }

    if (ImGui::Combo("Source", &source_index, items.data(), static_cast<int>(items.size())))
        m_source_pin = source_index == 0 ? -1 : pins[static_cast<size_t>(source_index - 1)].pin_id;

    const Spectrum *source = nullptr;
    for (const auto &pin : pins) {
        if (pin.pin_id == m_source_pin) {
            source = pin.spectrum;
            break;
        }
    }

    const PowerMeasurement measurement = m_engine.measure(source);

    if (!measurement.valid) {
        ImGui::TextDisabled("Unavailable: %s", powerMeterErrorMessage(measurement.error));
    } else if (std::isinf(measurement.power_dBm)) {
        ImGui::Text("Total Power: -inf dBm");
    } else {
        ImGui::Text("Total Power: %.2f dBm", measurement.power_dBm);
    }

    ImGui::End();
}
