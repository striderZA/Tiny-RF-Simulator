#include "inspector_panel.h"
#include "adc_engine.h"
#include "amplifier_engine.h"
#include "common.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "mixer_engine.h"
#include "pfb_channelizer_engine.h"
#include "ideal_filter_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include "coax_cable_engine.h"
#include "utils.h"
#include <cstring>
#include <portable-file-dialogs.h>

#include "component_registry.h"

InspectorPanel::InspectorPanel(NodeGraphEngine &graph, ComponentRegistry &components)
    : m_graph(graph), m_components(&components) {}

InspectorPanel::Hit InspectorPanel::findSelected() const {
    int n = ImNodes::NumSelectedNodes();
    if (n != 1)
        return {ComponentType::None, nullptr};

    int selected_id = -1;
    ImNodes::GetSelectedNodes(&selected_id);

    auto* engine = m_components->find(selected_id);
    if (!engine)
        return {ComponentType::None, nullptr};

         if (dynamic_cast<SignalGeneratorEngine*>(engine))       return {ComponentType::Generator, engine};
    else if (dynamic_cast<AmplifierEngine*>(engine))             return {ComponentType::Amplifier, engine};
    else if (dynamic_cast<SplitterEngine*>(engine))              return {ComponentType::Splitter, engine};
    else if (dynamic_cast<MixerEngine*>(engine))                 return {ComponentType::Mixer, engine};
    else if (dynamic_cast<AdcEngine*>(engine))                   return {ComponentType::Adc, engine};
    else if (dynamic_cast<PFBChannelizerEngine*>(engine))        return {ComponentType::PFB, engine};
    else if (dynamic_cast<IdealFilterEngine*>(engine))           return {ComponentType::IdealFilter, engine};
    else if (dynamic_cast<CoaxCableEngine*>(engine))            return {ComponentType::CoaxCable, engine};

    return {ComponentType::None, nullptr};
}

std::string InspectorPanel::labelForHit(const Hit& hit) const {
    if (hit.type == ComponentType::None || !hit.engine)
        return "";
    if (hit.type == ComponentType::PFB)
        return "PFB " + std::to_string(hit.engine->id());
    switch (hit.type) {
    case ComponentType::Amplifier:     return "Amplifier " + std::to_string(hit.engine->id());
    case ComponentType::Mixer:         return "Mixer " + std::to_string(hit.engine->id());
    case ComponentType::Splitter:      return "Splitter " + std::to_string(hit.engine->id());
    case ComponentType::Adc:           return "ADC " + std::to_string(hit.engine->id());
    case ComponentType::Generator:     return "Generator " + std::to_string(hit.engine->id());
        case ComponentType::IdealFilter:   return "IdealFilter " + std::to_string(hit.engine->id());
        case ComponentType::CoaxCable:     return "Coax Cable " + std::to_string(hit.engine->id());
    default:                           return "";
    }
}

void InspectorPanel::draw(const char *title, bool *p_open) {
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    int gid = m_graph.selectedGroupId();
    if (gid >= 0) {
        drawGroupPanel(gid);
        ImGui::End();
        return;
    }

    auto hit = findSelected();
    if (hit.type == ComponentType::None || !hit.engine) {
        ImGui::TextDisabled("Select a component in the Node Editor");

        ImGui::SeparatorText("View");
        if (m_viewToggles.log)
            ImGui::Checkbox("Log", m_viewToggles.log);
        if (m_viewToggles.node_editor)
            ImGui::Checkbox("Node Editor", m_viewToggles.node_editor);
        if (m_viewToggles.spectrum)
            ImGui::Checkbox("Spectrum Analyzer", m_viewToggles.spectrum);
        if (m_viewToggles.properties)
            ImGui::Checkbox("Properties", m_viewToggles.properties);
        if (m_viewToggles.iq_plot)
            ImGui::Checkbox("IQ Plot", m_viewToggles.iq_plot);

        ImGui::End();
        return;
    }

    // Build label from graph node
    int node_id = hit.engine->graphNodeId();

    ImGui::SeparatorText(labelForHit(hit).c_str());

    switch (hit.type) {
    case ComponentType::Amplifier:
        drawAmplifierProperties(*static_cast<AmplifierEngine*>(hit.engine), hit.engine->id());
        break;
    case ComponentType::Mixer:
        drawMixerProperties(*static_cast<MixerEngine*>(hit.engine), hit.engine->id());
        break;
    case ComponentType::Splitter:
        drawSplitterProperties(*static_cast<SplitterEngine*>(hit.engine), hit.engine->id());
        break;
    case ComponentType::Adc:
        drawAdcProperties(*static_cast<AdcEngine*>(hit.engine), hit.engine->id());
        break;
    case ComponentType::Generator:
        drawGeneratorProperties(*static_cast<SignalGeneratorEngine*>(hit.engine), hit.engine->id());
        break;
    case ComponentType::PFB: {
        auto* pfb = static_cast<PFBChannelizerEngine*>(hit.engine);
        for (int i = 0; i < static_cast<int>(m_pfb_ptrs.size()); ++i) {
            if (m_pfb_ptrs[i] == pfb) {
                m_selected_pfb_index = i;
                break;
            }
        }
        if (!m_pfb_ptrs.empty()) {
            int display_id = (m_selected_pfb_index < static_cast<int>(m_pfb_ptrs.size()) &&
                              m_pfb_ptrs[m_selected_pfb_index])
                                 ? m_pfb_ptrs[m_selected_pfb_index]->id()
                                 : m_selected_pfb_index;
            std::string combo_label = "PFB##selector";
            std::string preview = "PFB " + std::to_string(display_id);
            if (ImGui::BeginCombo(combo_label.c_str(), preview.c_str())) {
                for (int i = 0; i < static_cast<int>(m_pfb_ptrs.size()); ++i) {
                    if (!m_pfb_ptrs[i])
                        continue;
                    bool selected = (i == m_selected_pfb_index);
                    std::string item = "PFB " + std::to_string(m_pfb_ptrs[i]->id());
                    if (ImGui::Selectable(item.c_str(), &selected))
                        m_selected_pfb_index = i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (m_selected_pfb_index < static_cast<int>(m_pfb_ptrs.size()) &&
                m_pfb_ptrs[m_selected_pfb_index])
                drawPFBProperties(*m_pfb_ptrs[m_selected_pfb_index]);
        }
        break;
    }
    case ComponentType::IdealFilter:
        drawIdealFilterProperties(*static_cast<IdealFilterEngine*>(hit.engine), hit.engine->id());
        break;
    case ComponentType::CoaxCable:
        drawCoaxCableProperties(*static_cast<CoaxCableEngine*>(hit.engine), hit.engine->id());
        break;
    default:
        break;
    }

    ImGui::End();
}

void InspectorPanel::drawAmplifierProperties(AmplifierEngine &engine, int index) {
    (void)index;

    ImGui::SeparatorText("Mode");
    const char* amp_modes[] = {"Ideal", "S-Parameter"};
    int amp_mode_idx = engine.sparamMode() ? 1 : 0;
    if (ImGui::Combo("##amp_mode", &amp_mode_idx, amp_modes, IM_ARRAYSIZE(amp_modes))) {
        if (amp_mode_idx == 0)
            engine.setSParamMode(false);
    }

    if (amp_mode_idx == 1) {
        ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
        if (ImGui::Button("Browse##amp_sparam")) {
            auto result = pfd::open_file("Select S-parameter file", "",
                {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"}).result();
            if (!result.empty()) {
                engine.setSParamFilepath(result[0]);
                LOG_INFO("Amplifier S-param file: %s", result[0].c_str());
            }
        }
        if (engine.sparamLoaded()) {
            ImGui::TextDisabled("Points: %zu | Ports: %d",
                engine.sparamData().freqs().size(),
                engine.sparamData().numPorts());
        } else if (!engine.sparamFilepath().empty()) {
            ImGui::TextColored(ImVec4(1,0,0,1), "Failed to load file");
        }
    }

    if (engine.sparamMode()) {
        ImGui::BeginDisabled();
        double g = engine.gain_dB();
        utils::inputDouble("Gain (dB)", g, 1, 10, "%.1f", -10.0, 40.0);
        ImGui::EndDisabled();
    } else {
        double gain = engine.gain_dB();
        if (utils::inputDouble("Gain (dB)", gain, 1, 10, "%.1f", -10.0, 40.0))
            engine.setGain_dB(gain);
    }

    double nf = engine.nf_dB();
    if (utils::inputDouble("NF (dB)", nf, 0.1, 10, "%.1f", 0.0, 30.0))
        engine.setNF_dB(nf);

    bool nonlin = engine.enableNonlinear();
    if (ImGui::Checkbox("Enable Nonlinearity", &nonlin))
        engine.setEnableNonlinear(nonlin);

    if (nonlin) {
        double oip2 = engine.oip2_dBm();
        if (utils::inputDouble("OIP2 (dBm)", oip2, 1, 10, "%.1f", -100.0, 200.0))
            engine.setOIP2_dBm(oip2);

        double oip3 = engine.oip3_dBm();
        if (utils::inputDouble("OIP3 (dBm)", oip3, 1, 10, "%.1f", -100.0, 200.0))
            engine.setOIP3_dBm(oip3);

        double p1dB_est = oip3 - 10.0;
        ImGui::TextDisabled("P1dB ≈ %.1f dBm", p1dB_est);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Estimated 1 dB compression point");
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawMixerProperties(MixerEngine &engine, int index) {
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

void InspectorPanel::drawSplitterProperties(SplitterEngine &engine, int index) {
    (void)index;
    ImGui::TextDisabled("Splitter: 1 input, 2 outputs, -3 dB split loss");
    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawAdcProperties(AdcEngine &engine, int index) {
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
        if (bits < 1)
            bits = 1;
        if (bits > 24)
            bits = 24;
        engine.setBits(bits);
    }

    double vfs = engine.v_fs();
    ImGui::SetNextItemWidth(120.0f);
    if (utils::inputDouble("V_FS (V)", vfs, 0.1, 1.0, "%.2f", 0.1, 10.0))
        engine.setVfs(vfs);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawGeneratorProperties(SignalGeneratorEngine &engine, int index) {
    (void)index;
    ImGui::Checkbox("Measure", &engine.node().view_enabled);

    ImGui::TextDisabled("Output Sample Rate (for PFB chain):");
    double fs = engine.fs_Hz();
    if (utils::inputFrequency("Fs (MHz)", fs, 1.0, 100.0, "%.0f", 0.0, 100e9))
        engine.setFs_Hz(fs);

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
            ImGui::TableNextColumn();
            ImGui::Text("%d", i + 1);

            double freq = engine.tones()[i].freq_Hz;
            ImGui::TableNextColumn();
            bool f_ch =
                utils::inputFrequency("##freq", freq, 1.0, 100.0, "%.0f", MIN_FREQ, MAX_FREQ);

            double amp = engine.tones()[i].power_dBm;
            ImGui::TableNextColumn();
            bool a_ch = utils::inputDouble("##amp", amp, 1, 5, "%.0f", MIN_POWER, MAX_POWER);

            double phase = engine.tones()[i].phase_deg;
            ImGui::TableNextColumn();
            bool p_ch = utils::inputDouble("##phase", phase, 1, 10, "%.0f", -180.0, 180.0);

            if (f_ch || a_ch || p_ch)
                engine.updateTone(i, freq, amp, phase);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
                to_delete = i;
        }
        ImGui::EndTable();
        if (to_delete >= 0)
            engine.removeTone(to_delete);
    }

    if (ImGui::Button("+ Add Tone"))
        engine.addTone(100e6, -60.0);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawCoaxCableProperties(CoaxCableEngine& engine, int index) {
    (void)index;
    const CableSpec& p = engine.preset();

    // Preset combo
    {
        const char* preview = p.name;
        if (ImGui::BeginCombo("Model", preview)) {
            for (int i = 0; i < static_cast<int>(kCoaxCablePresets.size()); ++i) {
                bool selected = (i == engine.presetIndex());
                if (ImGui::Selectable(kCoaxCablePresets[i].name, selected))
                    engine.setPresetIndex(i);
                if (selected) ImGui::SetItemDefaultFocus();
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", kCoaxCablePresets[i].name);
                    ImGui::Text("K1 = %.6f dB/m", kCoaxCablePresets[i].K1_dB_per_m);
                    ImGui::Text("K2 = %.6f dB/m", kCoaxCablePresets[i].K2_dB_per_m);
                    ImGui::Text("Max freq: %.2f GHz", kCoaxCablePresets[i].max_freq_GHz);
                    ImGui::Text("Delay: %.3f ns/m", kCoaxCablePresets[i].delay_ns_per_m);
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndCombo();
        }
    }

    // Length
    double L = engine.lengthM();
    if (utils::inputDouble("Length (m)", L, 0.01, 1.0, "%.3f", 0.0, 1000.0))
        engine.setLengthM(L);

    // Connector loss
    double conn = engine.connectorsLossDB();
        if (utils::inputDouble("Connector Loss (dB)", conn, 0.1, 1.0, "%.2f", -100.0, 100.0))
        engine.setConnectorsLossDB(conn);

    // Read-out at input centre frequency
    if (!engine.node().inputs.empty() && engine.node().inputs[0] &&
        !engine.node().inputs[0]->frequencies.empty()) {
        const auto& fin = engine.node().inputs[0]->frequencies;
        const double fc = (fin.front() + fin.back()) / 2.0;
        const double fc_clamped = std::clamp(std::abs(fc), 1.0, p.max_freq_GHz * 1e9);
        const double fc_MHz = fc_clamped / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(fc_MHz) + p.K2_dB_per_m * fc_MHz) * engine.lengthM()
            + engine.connectorsLossDB();
        const double phase_shift =
            -360.0 * (fc_clamped / 1e9) * engine.lengthM() * p.delay_ns_per_m * 1e-3;
        ImGui::TextDisabled("Loss @ fc: %.3f dB", loss_dB);
        ImGui::TextDisabled("Phase shift @ fc: %.3f deg", phase_shift);
    } else {
        ImGui::TextDisabled("Loss @ fc: --");
        ImGui::TextDisabled("Phase shift @ fc: --");
    }

    ImGui::TextDisabled("Max freq: %.2f GHz  |  Delay: %.3f ns/m  |  Diameter: %.1f mm",
                        p.max_freq_GHz, p.delay_ns_per_m, p.diameter_mm);

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawIdealFilterProperties(IdealFilterEngine& engine, int index) {
    (void)index;
    ImGui::Text("Ideal Filter");
    ImGui::Separator();

    bool view = engine.node().view_enabled;
    if (ImGui::Checkbox("Measure", &view)) {
        engine.node().view_enabled = view;
    }

    ImGui::SeparatorText("Mode");
    const char* filter_modes[] = {"Ideal", "S-Parameter"};
    int f_mode_idx = engine.sparamMode() ? 1 : 0;
    if (ImGui::Combo("##filter_mode", &f_mode_idx, filter_modes, IM_ARRAYSIZE(filter_modes))) {
        engine.setSParamMode(f_mode_idx == 1);
    }

    if (engine.sparamMode()) {
        ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
        if (ImGui::Button("Browse##filter_sparam")) {
            auto result = pfd::open_file("Select S-parameter file", "",
                {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"}).result();
            if (!result.empty()) {
                engine.setSParamFilepath(result[0]);
            }
        }
        if (engine.sparamLoaded()) {
            ImGui::TextDisabled("Points: %zu | Ports: %d",
                engine.sparamData().freqs().size(),
                engine.sparamData().numPorts());
        }
        // Disable ideal-mode controls in S-param mode
        ImGui::BeginDisabled();
    }

    const char* type_names[] = {"LPF", "HPF", "BPF", "BSF"};
    int current = static_cast<int>(engine.filterType());
    if (ImGui::Combo("Type", &current, type_names, IM_ARRAYSIZE(type_names))) {
        engine.setFilterType(static_cast<FilterType>(current));
    }

    FilterType ft = engine.filterType();
    if (ft == FilterType::LPF || ft == FilterType::HPF) {
        double fc = engine.fcLow_Hz();
        if (utils::inputFrequency("Cutoff", fc, 0.0, 2000.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
            engine.setCutoff_Hz(fc);
        }
    } else {
        double low = engine.fcLow_Hz();
        double high = engine.fcHigh_Hz();
        bool changed = false;
        if (utils::inputFrequency("Low Cutoff", low, 0.0, 2000.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
            changed = true;
        }
        if (utils::inputFrequency("High Cutoff", high, 0.0, 2000.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
            changed = true;
        }
        if (changed) {
            engine.setCutoffs_Hz(low, high);
        }
    }

    if (engine.sparamMode()) {
        ImGui::EndDisabled();
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawGroupPanel(int group_id) {
    const Group* g = m_graph.groupById(group_id);
    if (!g) {
        m_graph.setSelectedGroupId(-1);
        return;
    }

    static char name_buf[128];
    static int last_gid = -1;
    if (last_gid != group_id) {
        std::strncpy(name_buf, g->name.c_str(), sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        last_gid = group_id;
    }

    ImGui::InputText("Name", name_buf, sizeof(name_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        m_graph.renameGroup(group_id, name_buf);
    }

    ImGui::Separator();
    ImGui::Text("Members (%zu):", g->member_node_ids.size());
    ImGui::Indent();
    for (int nid : g->member_node_ids) {
        for (const auto& n : m_graph.nodes()) {
            if (n.node_id == nid) {
                ImGui::TextUnformatted(n.label.c_str());
                break;
            }
        }
    }
    ImGui::Unindent();

    if (!g->boundary_pins.empty()) {
        ImGui::Separator();
        ImGui::Text("Boundary pins:");
        ImGui::Indent();
        for (const auto& bp : g->boundary_pins) {
            const char* dir = bp.is_output ? "OUT" : "IN";
            ImGui::Text("%s -> \"%s\"", dir, bp.label.c_str());
        }
        ImGui::Unindent();
    }

    ImGui::Separator();
    if (ImGui::Button("Ungroup")) {
        m_graph.removeGroup(group_id);
        last_gid = -1;
    }
}

void InspectorPanel::drawPFBProperties(PFBChannelizerEngine &engine) {
    int M = engine.channelCount();
    int K = engine.tapsPerBranch();
    float beta = static_cast<float>(engine.kaiserBeta());
    int ch = engine.activeChannel();

    if (ImGui::InputInt("Channels (M)", &M)) {
        if (M < 2)
            M = 2;
        if (M > 2048)
            M = 2048;
        engine.setChannelCount(M);
    }

    if (ImGui::InputInt("Taps/Branch (K)", &K)) {
        if (K < 1)
            K = 1;
        if (K > 64)
            K = 64;
        engine.setTapsPerBranch(K);
    }

    if (ImGui::SliderFloat("Kaiser Beta", &beta, 0.0f, 20.0f, "%.1f"))
        engine.setKaiserBeta(beta);

    if (ImGui::SliderInt("Active Channel", &ch, 0, engine.channelCount() - 1))
        engine.setActiveChannel(ch);

    const auto &channels = engine.channels();
    if (!channels.empty()) {
        const auto &active = channels[engine.activeChannel()];
        ImGui::Text("Ch %d: centre %.2f MHz, %.0f kHz BW", active.channel_index,
                    active.center_freq_Hz / 1e6, active.bandwidth_Hz / 1e3);
        ImGui::Text("Bins in channel: %zu", active.bin_indices.size());
        ImGui::Text("Tones: %zu", active.tones.size());
        ImGui::Text("Noise: %.3e W", active.noise_W);
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
