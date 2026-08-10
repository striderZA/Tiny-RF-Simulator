#include "inspector_panel.h"
#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "common.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "mixer_engine.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include "utils.h"
#include <cstring>
#include <portable-file-dialogs.h>

#include "component_registry.h"

InspectorPanel::InspectorPanel(NodeGraphEngine &graph, ComponentRegistry &components)
    : m_graph(graph), m_components(&components) {}

void InspectorPanel::registerDrawers(ComponentTypeRegistry &registry) {
    for (auto *d : registry.all()) {
        if (d->type == "generator") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawGeneratorProperties(static_cast<SignalGeneratorEngine &>(e), e.id());
            };
        } else if (d->type == "amplifier") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawAmplifierProperties(static_cast<AmplifierEngine &>(e), e.id());
            };
        } else if (d->type == "splitter") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawSplitterProperties(static_cast<SplitterEngine &>(e), e.id());
            };
        } else if (d->type == "mixer") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawMixerProperties(static_cast<MixerEngine &>(e), e.id());
            };
        } else if (d->type == "adc") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawAdcProperties(static_cast<AdcEngine &>(e), e.id());
            };
        } else if (d->type == "pfb") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawPFBProperties(static_cast<PFBChannelizerEngine &>(e));
            };
        } else if (d->type == "filter") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawIdealFilterProperties(static_cast<IdealFilterEngine &>(e), e.id());
            };
        } else if (d->type == "coax") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawCoaxCableProperties(static_cast<CoaxCableEngine &>(e), e.id());
            };
        } else if (d->type == "equalizer") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawEqualizerProperties(static_cast<EqualizerEngine &>(e), e.id());
            };
        } else if (d->type == "attenuator") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawAttenuatorProperties(static_cast<AttenuatorEngine &>(e), e.id());
            };
        } else if (d->type == "combiner") {
            d->draw_inspector = [](InspectorPanel &p, IComponentEngine &e) {
                p.drawCombinerProperties(static_cast<CombinerEngine &>(e), e.id());
            };
        }
    }
}

InspectorPanel::Hit InspectorPanel::findSelected() const {
    int n = ImNodes::NumSelectedNodes();
    if (n != 1)
        return {nullptr, nullptr};

    int selected_id = -1;
    ImNodes::GetSelectedNodes(&selected_id);

    auto *engine = m_components->find(selected_id);
    if (!engine)
        return {nullptr, nullptr};

    return {ComponentTypeRegistry::instance().find(engine->type_name()), engine};
}

std::string InspectorPanel::labelForHit(const Hit &hit) const {
    if (!hit.desc || !hit.engine)
        return "";
    return hit.desc->display_name + " " + std::to_string(hit.engine->id());
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
    if (!hit.desc || !hit.engine) {
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

    m_param_edited = false;

    // The PFB multi-instance combo decides which PFB the panel edits. Keeping
    // a single "edited engine" means the header, the property controls, and the
    // Show-IQ-Plot / Show-Channelizer-Grid checkboxes always refer to the same
    // PFB. The graph-selected PFB is the default until the combo is used.
    IComponentEngine *edit_engine = hit.engine;
    if (hit.desc->type == "pfb") {
        auto *pfb = static_cast<PFBChannelizerEngine *>(hit.engine);
        if (m_pfb_combo_graph_id != pfb->id()) {
            // Graph selection moved to a different PFB (or the anchor was
            // dropped after an add/remove): follow the graph selection.
            m_pfb_combo_graph_id = pfb->id();
            m_selected_pfb_index = -1;
            for (int i = 0; i < static_cast<int>(m_pfb_ptrs.size()); ++i) {
                if (m_pfb_ptrs[i] == pfb) {
                    m_selected_pfb_index = i;
                    break;
                }
            }
        }
        if (m_selected_pfb_index >= 0 &&
            m_selected_pfb_index < static_cast<int>(m_pfb_ptrs.size()) &&
            m_pfb_ptrs[m_selected_pfb_index])
            edit_engine = m_pfb_ptrs[m_selected_pfb_index];
    }

    ImGui::SeparatorText(labelForHit(Hit{hit.desc, edit_engine}).c_str());

    if (hit.desc->type == "pfb" && !m_pfb_ptrs.empty()) {
        std::string combo_label = "PFB##selector";
        std::string preview = "PFB " + std::to_string(edit_engine->id());
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
    }

    if (hit.desc->draw_inspector)
        hit.desc->draw_inspector(*this, *edit_engine);

    if (m_param_edited && onParamChange)
        onParamChange();

    ImGui::End();
}

void InspectorPanel::drawAmplifierProperties(AmplifierEngine &engine, int index) {
    (void)index;

    ImGui::SeparatorText("Mode");
    const char *amp_modes[] = {"Ideal", "S-Parameter"};
    int amp_mode_idx = engine.sparamMode() ? 1 : 0;
    if (ImGui::Combo("##amp_mode", &amp_mode_idx, amp_modes, IM_ARRAYSIZE(amp_modes))) {
        engine.setSParamMode(amp_mode_idx == 1);
        m_param_edited = true;
    }

    if (amp_mode_idx == 1) {
        ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
        if (ImGui::Button("Browse##amp_sparam")) {
            auto result = pfd::open_file("Select S-parameter file", "",
                                         {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"})
                              .result();
            if (!result.empty()) {
                engine.setSParamFilepath(result[0]);
                LOG_INFO("Amplifier S-param file: %s", result[0].c_str());
                m_param_edited = true;
            }
        }
        if (engine.sparamLoaded()) {
            ImGui::TextDisabled("Points: %zu | Ports: %d", engine.sparamData().freqs().size(),
                                engine.sparamData().numPorts());
        } else if (!engine.sparamFilepath().empty()) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load file");
        }
    }

    if (engine.sparamMode()) {
        ImGui::BeginDisabled();
        double g = engine.gain_dB();
        utils::inputDouble("Gain (dB)", g, 1, 10, "%.1f", -10.0, 40.0);
        ImGui::EndDisabled();
    } else {
        double gain = engine.gain_dB();
        if (utils::inputDouble("Gain (dB)", gain, 1, 10, "%.1f", -10.0, 40.0)) {
            engine.setGain_dB(gain);
            m_param_edited = true;
        }
    }

    double nf = engine.nf_dB();
    if (utils::inputDouble("NF (dB)", nf, 0.1, 10, "%.1f", 0.0, 30.0)) {
        engine.setNF_dB(nf);
        m_param_edited = true;
    }

    bool nonlin = engine.enableNonlinear();
    if (ImGui::Checkbox("Enable Nonlinearity", &nonlin)) {
        engine.setEnableNonlinear(nonlin);
        m_param_edited = true;
    }

    if (nonlin) {
        double oip2 = engine.oip2_dBm();
        if (utils::inputDouble("OIP2 (dBm)", oip2, 1, 10, "%.1f", -100.0, 200.0)) {
            engine.setOIP2_dBm(oip2);
            m_param_edited = true;
        }

        double oip3 = engine.oip3_dBm();
        if (utils::inputDouble("OIP3 (dBm)", oip3, 1, 10, "%.1f", -100.0, 200.0)) {
            engine.setOIP3_dBm(oip3);
            m_param_edited = true;
        }

        double p1db = engine.p1db_dBm();
        if (utils::inputDouble("P1dB (dBm)", p1db, 1, 10, "%.1f", -100.0, 200.0)) {
            engine.setP1dB_dBm(p1db);
            m_param_edited = true;
        }
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawMixerProperties(MixerEngine &engine, int index) {
    (void)index;
    double lo = engine.loFreq_Hz();
    if (utils::inputFrequency("LO Frequency (MHz)", lo, 1.0, 100.0, "%.0f", 0.0, 100e9)) {
        engine.setLoFreq_Hz(lo);
        m_param_edited = true;
    }

    double gain = engine.conversionGain_dB();
    if (utils::inputDouble("Conv. Gain (dB)", gain, 1, 10, "%.1f", -30.0, 30.0)) {
        engine.setConversionGain_dB(gain);
        m_param_edited = true;
    }

    double nf = engine.nf_dB();
    if (utils::inputDouble("NF (dB)", nf, 0.1, 10, "%.1f", 0.0, 30.0)) {
        engine.setNF_dB(nf);
        m_param_edited = true;
    }

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
    if (utils::inputFrequency("Fs (MHz)", fs, 1.0, 100.0, "%.0f", 1e3, 1e12)) {
        engine.setFs_Hz(fs);
        m_param_edited = true;
    }

    double nsd = engine.nsd_dBm_per_Hz();
    if (utils::inputDouble("NSD (dBm/Hz)", nsd, 1, 10, "%.1f", -250.0, -30.0)) {
        engine.setNsd_dBm_per_Hz(nsd);
        m_param_edited = true;
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawGeneratorProperties(SignalGeneratorEngine &engine, int index) {
    (void)index;
    if (ImGui::Checkbox("Measure", &engine.node().view_enabled)) {
        m_param_edited = true;
    }

    ImGui::TextDisabled("Output Sample Rate (for PFB chain):");
    double fs = engine.fs_Hz();
    if (utils::inputFrequency("Fs (MHz)", fs, 1.0, 100.0, "%.0f", 0.0, 100e9)) {
        engine.setFs_Hz(fs);
        m_param_edited = true;
    }

    if (ImGui::BeginTable("gen_tones", 5, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Frequency (MHz)");
        ImGui::TableSetupColumn("Amplitude (dBm)");
        ImGui::TableSetupColumn("Phase (deg)");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableHeadersRow();

        int to_delete = -1;
        for (int i = 0; i < static_cast<int>(engine.toneCount()); ++i) {
            ImGui::PushID(i);
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

            if (f_ch || a_ch || p_ch) {
                engine.updateTone(i, freq, amp, phase);
                m_param_edited = true;
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
                to_delete = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (to_delete >= 0) {
            engine.removeTone(to_delete);
            m_param_edited = true;
        }
    }

    if (ImGui::Button("+ Add Tone")) {
        engine.addTone(100e6, -60.0);
        m_param_edited = true;
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawCoaxCableProperties(CoaxCableEngine &engine, int index) {
    (void)index;
    const CableSpec &p = engine.preset();

    // Preset combo
    {
        const char *preview = p.name;
        if (ImGui::BeginCombo("Model", preview)) {
            for (int i = 0; i < static_cast<int>(kCoaxCablePresets.size()); ++i) {
                bool selected = (i == engine.presetIndex());
                if (ImGui::Selectable(kCoaxCablePresets[i].name, selected)) {
                    engine.setPresetIndex(i);
                    m_param_edited = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
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
    if (utils::inputDouble("Length (m)", L, 0.01, 1.0, "%.3f", 0.0, 1000.0)) {
        engine.setLengthM(L);
        m_param_edited = true;
    }

    // Connector loss
    double conn = engine.connectorsLossDB();
    if (utils::inputDouble("Connector Loss (dB)", conn, 0.1, 1.0, "%.2f", -100.0, 100.0)) {
        engine.setConnectorsLossDB(conn);
        m_param_edited = true;
    }

    // Read-out at input centre frequency
    if (!engine.node().inputs.empty() && engine.node().inputs[0] &&
        !engine.node().inputs[0]->frequencies.empty()) {
        const auto &fin = engine.node().inputs[0]->frequencies;
        const double fc = (fin.front() + fin.back()) / 2.0;
        const double fc_clamped = std::clamp(std::abs(fc), 1.0, p.max_freq_GHz * 1e9);
        const double fc_MHz = fc_clamped / 1e6;
        const double loss_dB =
            (p.K1_dB_per_m * std::sqrt(fc_MHz) + p.K2_dB_per_m * fc_MHz) * engine.lengthM() +
            engine.connectorsLossDB();
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

void InspectorPanel::drawEqualizerProperties(EqualizerEngine &engine, int index) {
    (void)index;

    ImGui::SeparatorText("Mode");
    const char *eq_modes[] = {"Ideal", "S-Parameter"};
    int eq_mode_idx = engine.sparamMode() ? 1 : 0;
    if (ImGui::Combo("##eq_mode", &eq_mode_idx, eq_modes, IM_ARRAYSIZE(eq_modes))) {
        engine.setSParamMode(eq_mode_idx == 1);
        m_param_edited = true;
    }

    if (engine.sparamMode()) {
        ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
        if (ImGui::Button("Browse##eq_sparam")) {
            auto result = pfd::open_file("Select S-parameter file", "",
                                         {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"})
                              .result();
            if (!result.empty()) {
                engine.setSParamFilepath(result[0]);
                m_param_edited = true;
            }
        }
        if (engine.sparamLoaded()) {
            ImGui::TextDisabled("Points: %zu | Ports: %d", engine.sparamData().freqs().size(),
                                engine.sparamData().numPorts());
        } else if (!engine.sparamFilepath().empty()) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load file");
        }
        ImGui::BeginDisabled();
    }

    double ref_gain = engine.refGain_dB();
    if (utils::inputDouble("Ref Gain (dB)", ref_gain, 1, 10, "%.1f", -40.0, 40.0)) {
        engine.setRefGain_dB(ref_gain);
        m_param_edited = true;
    }

    double ref_freq = engine.refFreq_Hz();
    if (utils::inputFrequency("Ref Freq (MHz)", ref_freq, 1.0, 100.0, "%.0f", 1.0, 100e9)) {
        engine.setRefFreq_Hz(ref_freq);
        m_param_edited = true;
    }

    double slope = engine.slope_dBPerDecade();
    if (utils::inputDouble("Slope (dB/dec)", slope, 0.1, 1.0, "%.1f", -100.0, 100.0)) {
        engine.setSlope_dBPerDecade(slope);
        m_param_edited = true;
    }

    if (engine.sparamMode())
        ImGui::EndDisabled();

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawIdealFilterProperties(IdealFilterEngine &engine, int index) {
    (void)index;
    ImGui::Text("Ideal Filter");
    ImGui::Separator();

    bool view = engine.node().view_enabled;
    if (ImGui::Checkbox("Measure", &view)) {
        engine.node().view_enabled = view;
        m_param_edited = true;
    }

    ImGui::SeparatorText("Mode");
    const char *filter_modes[] = {"Ideal", "S-Parameter"};
    int f_mode_idx = engine.sparamMode() ? 1 : 0;
    if (ImGui::Combo("##filter_mode", &f_mode_idx, filter_modes, IM_ARRAYSIZE(filter_modes))) {
        engine.setSParamMode(f_mode_idx == 1);
        m_param_edited = true;
    }

    if (engine.sparamMode()) {
        ImGui::TextWrapped("File: %s", engine.sparamFilepath().c_str());
        if (ImGui::Button("Browse##filter_sparam")) {
            auto result = pfd::open_file("Select S-parameter file", "",
                                         {"S-parameter Files", "*.s2p *.s3p *.s4p *.sNp"})
                              .result();
            if (!result.empty()) {
                engine.setSParamFilepath(result[0]);
                m_param_edited = true;
            }
        }
        if (engine.sparamLoaded()) {
            ImGui::TextDisabled("Points: %zu | Ports: %d", engine.sparamData().freqs().size(),
                                engine.sparamData().numPorts());
        }
        // Disable ideal-mode controls in S-param mode
        ImGui::BeginDisabled();
    }

    const char *type_names[] = {"LPF", "HPF", "BPF", "BSF"};
    int current = static_cast<int>(engine.filterType());
    if (ImGui::Combo("Type", &current, type_names, IM_ARRAYSIZE(type_names))) {
        engine.setFilterType(static_cast<FilterType>(current));
        m_param_edited = true;
    }

    FilterType ft = engine.filterType();
    if (ft == FilterType::LPF || ft == FilterType::HPF) {
        double fc = engine.fcLow_Hz();
        if (utils::inputFrequency("Cutoff", fc, 0.0, 2000.0, "%.0f", MIN_FREQ, MAX_FREQ)) {
            engine.setCutoff_Hz(fc);
            m_param_edited = true;
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
            m_param_edited = true;
        }
    }

    if (engine.sparamMode()) {
        ImGui::EndDisabled();
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
void InspectorPanel::drawAttenuatorProperties(AttenuatorEngine &engine, int index) {
    (void)index;

    float atten_f = static_cast<float>(engine.attenuation());
    if (ImGui::DragFloat("Atten (dB)", &atten_f, 0.1f, 0.0f, 200.0f)) {
        engine.setAttenuation(static_cast<double>(atten_f));
        m_param_edited = true;
    }

    bool sparam_mode = engine.sParamMode();
    if (ImGui::Checkbox("S-param mode", &sparam_mode)) {
        engine.setSParamMode(sparam_mode);
        m_param_edited = true;
    }

    if (sparam_mode) {
        std::string path = engine.sParamFile();
        char path_buf[512];
        strncpy(path_buf, path.c_str(), sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
        if (ImGui::InputText("S-param file", path_buf, sizeof(path_buf))) {
            engine.setSParamFile(path_buf);
            m_param_edited = true;
        }
    }

    ImGui::Text("NF = %.2f dB", engine.attenuation());

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawCombinerProperties(CombinerEngine &engine, int index) {
    (void)index;

    ImGui::TextDisabled("Combiner: 2 inputs → 1 output");

    bool sparam_mode = engine.sParamMode();
    if (ImGui::Checkbox("S-parameter mode", &sparam_mode)) {
        engine.setSParamMode(sparam_mode);
        m_param_edited = true;
    }

    if (sparam_mode) {
        std::string path = engine.sParamFile();
        char path_buf[512];
        strncpy(path_buf, path.c_str(), sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
        if (ImGui::InputText("S-param file", path_buf, sizeof(path_buf))) {
            engine.setSParamFile(path_buf);
            m_param_edited = true;
        }
    }

    ImGui::Text("Loss: -3 dB per input");

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}

void InspectorPanel::drawGroupPanel(int group_id) {
    const Group *g = m_graph.groupById(group_id);
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
        m_param_edited = true;
    }

    ImGui::Separator();
    ImGui::Text("Members (%zu):", g->member_node_ids.size());
    ImGui::Indent();
    for (int nid : g->member_node_ids) {
        for (const auto &n : m_graph.nodes()) {
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
        for (const auto &bp : g->boundary_pins) {
            const char *dir = bp.is_output ? "OUT" : "IN";
            ImGui::Text("%s -> \"%s\"", dir, bp.label.c_str());
        }
        ImGui::Unindent();
    }

    ImGui::Separator();
    if (ImGui::Button("Ungroup")) {
        m_graph.removeGroup(group_id);
        last_gid = -1;
        m_param_edited = true;
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
        m_param_edited = true;
    }

    if (ImGui::InputInt("Taps/Branch (K)", &K)) {
        if (K < 1)
            K = 1;
        if (K > 64)
            K = 64;
        engine.setTapsPerBranch(K);
        m_param_edited = true;
    }

    if (ImGui::SliderFloat("Kaiser Beta", &beta, 0.0f, 20.0f, "%.1f")) {
        engine.setKaiserBeta(beta);
        m_param_edited = true;
    }

    if (ImGui::SliderInt("Active Channel", &ch, 0, engine.channelCount() - 1)) {
        engine.setActiveChannel(ch);
        m_param_edited = true;
    }

    const auto &channels = engine.channels();
    if (!channels.empty()) {
        const auto &active = channels[engine.activeChannel()];
        ImGui::Text("Ch %d: centre %.2f MHz, %.0f kHz BW", active.channel_index,
                    active.center_freq_Hz / 1e6, active.bandwidth_Hz / 1e3);
        ImGui::Text("Bins in channel: %zu", active.bin_indices.size());
        ImGui::Text("Tones: %zu", active.tones.size());
        ImGui::Text("Noise: %.3e W", active.noise_W);
    }

    if (m_pfb_iq_visible && m_selected_pfb_index >= 0 &&
        m_selected_pfb_index < static_cast<int>(m_pfb_iq_visible->size())) {
        bool show = (*m_pfb_iq_visible)[m_selected_pfb_index];
        if (ImGui::Checkbox("Show IQ Plot", &show))
            (*m_pfb_iq_visible)[m_selected_pfb_index] = show;
    }
    if (m_pfb_grid_visible && m_selected_pfb_index >= 0 &&
        m_selected_pfb_index < static_cast<int>(m_pfb_grid_visible->size())) {
        bool show = (*m_pfb_grid_visible)[m_selected_pfb_index];
        if (ImGui::Checkbox("Show Channelizer Grid", &show))
            (*m_pfb_grid_visible)[m_selected_pfb_index] = show;
    }

    if (ImGui::Button("Delete") && onRemoveNode)
        onRemoveNode(engine.graphNodeId());
}
