#include "inspector_panel.h"
#include "adc_engine.h"
#include "amplifier_engine.h"
#include "mixer_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include "s_parameter_amplifier_engine.h"
#include "s_parameter_filter_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "utils.h"
#include "common.h"

InspectorPanel::InspectorPanel(
    NodeGraphEngine& graph,
    std::vector<std::unique_ptr<AmplifierEngine>>& amps,
    std::vector<std::unique_ptr<MixerEngine>>& mixers,
    std::vector<std::unique_ptr<SplitterEngine>>& splitters,
    std::vector<std::unique_ptr<SParameterAmplifierEngine>>& sparam_amps,
    std::vector<std::unique_ptr<SParameterFilterEngine>>& sparam_filters,
    std::vector<std::unique_ptr<AdcEngine>>& adcs,
    std::vector<std::unique_ptr<SignalGeneratorEngine>>& generators
) : m_graph(graph), m_amplifiers(amps), m_mixers(mixers), m_splitters(splitters),
    m_sparam_amps(sparam_amps), m_sparam_filters(sparam_filters), m_adcs(adcs), m_generators(generators) {}

InspectorPanel::Hit InspectorPanel::findSelected() const {
    int n = ImNodes::NumSelectedNodes();
    if (n != 1) return {ComponentType::None, -1};

    int selected_id = -1;
    ImNodes::GetSelectedNodes(&selected_id);

    for (int i = 0; i < static_cast<int>(m_generators.size()); ++i)
        if (m_generators[i]->graphNodeId() == selected_id) return {ComponentType::Generator, i};
    for (int i = 0; i < static_cast<int>(m_amplifiers.size()); ++i)
        if (m_amplifiers[i]->graphNodeId() == selected_id) return {ComponentType::Amplifier, i};
    for (int i = 0; i < static_cast<int>(m_splitters.size()); ++i)
        if (m_splitters[i]->graphNodeId() == selected_id) return {ComponentType::Splitter, i};
    for (int i = 0; i < static_cast<int>(m_mixers.size()); ++i)
        if (m_mixers[i]->graphNodeId() == selected_id) return {ComponentType::Mixer, i};
    for (int i = 0; i < static_cast<int>(m_sparam_amps.size()); ++i)
        if (m_sparam_amps[i]->graphNodeId() == selected_id) return {ComponentType::SParamAmp, i};
    for (int i = 0; i < static_cast<int>(m_sparam_filters.size()); ++i)
        if (m_sparam_filters[i]->graphNodeId() == selected_id) return {ComponentType::SParamFilter, i};
    for (int i = 0; i < static_cast<int>(m_adcs.size()); ++i)
        if (m_adcs[i]->graphNodeId() == selected_id) return {ComponentType::Adc, i};

    return {ComponentType::None, -1};
}

void InspectorPanel::draw(const char* title, bool* p_open) {
    if (!ImGui::Begin(title, p_open)) { ImGui::End(); return; }

    auto hit = findSelected();
    if (hit.type == ComponentType::None || hit.index < 0) {
        ImGui::TextDisabled("Select a component in the Node Editor");
        ImGui::End();
        return;
    }

    // Build label from graph node
    std::string label;
    int node_id = -1;
    switch (hit.type) {
        case ComponentType::Amplifier: node_id = m_amplifiers[hit.index]->graphNodeId(); label = "Amplifier " + std::to_string(m_amplifiers[hit.index]->id()); break;
        case ComponentType::Mixer:     node_id = m_mixers[hit.index]->graphNodeId(); label = "Mixer " + std::to_string(m_mixers[hit.index]->id()); break;
        case ComponentType::Splitter:  node_id = m_splitters[hit.index]->graphNodeId(); label = "Splitter " + std::to_string(m_splitters[hit.index]->id()); break;
        case ComponentType::SParamAmp: node_id = m_sparam_amps[hit.index]->graphNodeId(); label = "S-Param Amp " + std::to_string(m_sparam_amps[hit.index]->id()); break;
        case ComponentType::SParamFilter: node_id = m_sparam_filters[hit.index]->graphNodeId(); label = "S-Param Filter " + std::to_string(m_sparam_filters[hit.index]->id()); break;
        case ComponentType::Adc:       node_id = m_adcs[hit.index]->graphNodeId(); label = "ADC " + std::to_string(m_adcs[hit.index]->id()); break;
        case ComponentType::Generator: node_id = m_generators[hit.index]->graphNodeId(); label = "Generator " + std::to_string(m_generators[hit.index]->id()); break;
        default: break;
    }

    ImGui::SeparatorText(label.c_str());

    switch (hit.type) {
        case ComponentType::Amplifier: drawAmplifierProperties(*m_amplifiers[hit.index], hit.index); break;
        case ComponentType::Mixer:     drawMixerProperties(*m_mixers[hit.index], hit.index); break;
        case ComponentType::Splitter:  drawSplitterProperties(*m_splitters[hit.index], hit.index); break;
        case ComponentType::SParamAmp: drawSParamAmpProperties(*m_sparam_amps[hit.index], hit.index); break;
        case ComponentType::SParamFilter: drawSParamFilterProperties(*m_sparam_filters[hit.index], hit.index); break;
        case ComponentType::Adc:       drawAdcProperties(*m_adcs[hit.index], hit.index); break;
        case ComponentType::Generator: drawGeneratorProperties(*m_generators[hit.index], hit.index); break;
        default: break;
    }

    ImGui::End();
}

void InspectorPanel::drawAmplifierProperties(AmplifierEngine& engine, int index) {
    (void)index;
    double gain = engine.gain_dB();
    if (utils::inputDouble("Gain (dB)", gain, 1, 10, "%.1f", -10.0, 40.0))
        engine.setGain_dB(gain);

    double nf = engine.nf_dB();
    if (utils::inputDouble("NF (dB)", nf, 0.1, 10, "%.1f", 0.0, 30.0))
        engine.setNF_dB(nf);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawMixerProperties(MixerEngine& engine, int index) {
    (void)index;
    double lo = engine.loFreq_Hz();
    if (utils::inputFrequency("LO Frequency (MHz)", lo, 1.0, 100.0, "%.0f", 0.0, 100e9))
        engine.setLoFreq_Hz(lo);

    double gain = engine.conversionGain_dB();
    if (utils::inputDouble("Conv. Gain (dB)", gain, 1, 10, "%.1f", -30.0, 30.0))
        engine.setConversionGain_dB(gain);

    double nf = engine.nf_dB();
    if (utils::inputDouble("NF (dB)", nf, 0.1, 10, "%.1f", 0.0, 30.0))
        engine.setNF_dB(nf);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawSplitterProperties(SplitterEngine& engine, int index) {
    (void)index;
    ImGui::TextDisabled("Splitter: 1 input, 2 outputs, -3 dB split loss");
    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawSParamAmpProperties(SParameterAmplifierEngine& engine, int index) {
    (void)index;
    if (!engine.loaded()) {
        ImGui::TextColored(ImVec4(1,0,0,1), "Failed to load S-parameter file");
        return;
    }

    int np = engine.numPorts();
    int fwd_idx = engine.forwardParamIdx();
    std::string preview = "S" + std::to_string((fwd_idx / np) + 1) + std::to_string((fwd_idx % np) + 1);
    if (ImGui::BeginCombo("Forward Param", preview.c_str())) {
        for (int pi = 0; pi < np * np; ++pi) {
            std::string lbl = "S" + std::to_string((pi / np) + 1) + std::to_string((pi % np) + 1);
            if (ImGui::Selectable(lbl.c_str(), pi == fwd_idx))
                engine.setForwardParamIdx(pi);
        }
        ImGui::EndCombo();
    }

    ImGui::Text("Ports: %d | Data points: %zu", np, engine.freqs().size());
    ImGui::Text("Max freq: %.0f MHz", engine.freqs().back() / 1e6);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawSParamFilterProperties(SParameterFilterEngine& engine, int index) {
    (void)index;
    if (!engine.loaded()) {
        ImGui::TextColored(ImVec4(1,0,0,1), "Failed to load S-parameter file");
        return;
    }

    int np = engine.data().numPorts();
    ImGui::Text("Ports: %d | Data points: %zu", np, engine.data().freqs().size());
    ImGui::Text("Max freq: %.0f MHz", engine.data().freqs().back() / 1e6);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawAdcProperties(AdcEngine& engine, int index) {
    (void)index;
    double fs = engine.fs_Hz();
    if (utils::inputFrequency("Fs (MHz)", fs, 1.0, 100.0, "%.0f", 1e3, 1e12))
        engine.setFs_Hz(fs);

    double nsd = engine.nsd_dBm_per_Hz();
    if (utils::inputDouble("NSD (dBm/Hz)", nsd, 1, 10, "%.1f", -250.0, -30.0))
        engine.setNsd_dBm_per_Hz(nsd);

    int bits = engine.bits();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Bits", &bits)) {
        if (bits < 1) bits = 1;
        if (bits > 24) bits = 24;
        engine.setBits(bits);
    }

    double vfs = engine.v_fs();
    ImGui::SetNextItemWidth(120.0f);
    if (utils::inputDouble("V_FS (V)", vfs, 0.1, 1.0, "%.2f", 0.1, 10.0))
        engine.setVfs(vfs);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawGeneratorProperties(SignalGeneratorEngine& engine, int index) {
    (void)index;
    ImGui::Checkbox("Measure", &engine.node().view_enabled);

    if (ImGui::BeginTable("gen_tones", 5, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Frequency (MHz)");
        ImGui::TableSetupColumn("Amplitude (dBm)");
        ImGui::TableSetupColumn("Phase (deg)");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableHeadersRow();

        int to_delete = -1;
        for (int i = 0; i < static_cast<int>(engine.toneCount()); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);

            double freq = engine.tones()[i].freq_Hz;
            ImGui::TableNextColumn();
            bool f_ch = utils::inputFrequency("##freq", freq, 1.0, 100.0, "%.0f", MIN_FREQ, MAX_FREQ);

            double amp = engine.tones()[i].power_dBm;
            ImGui::TableNextColumn();
            bool a_ch = utils::inputDouble("##amp", amp, 1, 5, "%.0f", MIN_POWER, MAX_POWER);

            double phase = engine.tones()[i].phase_deg;
            ImGui::TableNextColumn();
            bool p_ch = utils::inputDouble("##phase", phase, 1, 10, "%.0f", -180.0, 180.0);

            if (f_ch || a_ch || p_ch)
                engine.updateTone(i, freq, amp, phase);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X")) to_delete = i;
        }
        ImGui::EndTable();
        if (to_delete >= 0) engine.removeTone(to_delete);
    }

    if (ImGui::Button("+ Add Tone"))
        engine.addTone(100e6, -60.0);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
