// app/src/component_type_registry.cpp
#include "component_type_registry.h"

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "component_registry.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "pfb_channelizer_engine.h"
#include "rf_switch_2to1_engine.h"
#include "rf_switch_engine.h"
#include "signal_generator_engine.h"
#include "splitter_engine.h"
#include <algorithm>
#include <utility>

namespace {

template <typename Engine>
bool loadSparamFile(IComponentEngine &component, const std::string &path) {
    auto *engine = dynamic_cast<Engine *>(&component);
    if (!engine)
        return false;
    engine->setSParamFilepath(path);
    return engine->sparamMode();
}

ParameterField stateField(std::string key, std::string label, std::string unit, FieldKind kind,
                          std::string help, std::vector<std::string> enum_values = {},
                          bool read_only = false) {
    ParameterField field;
    field.key = std::move(key);
    field.label = std::move(label);
    field.unit = std::move(unit);
    field.kind = kind;
    field.enum_values = std::move(enum_values);
    field.help = std::move(help);
    field.read_only = read_only;
    return field;
}
} // namespace

ComponentTypeRegistry &ComponentTypeRegistry::instance() {
    static ComponentTypeRegistry reg;
    return reg;
}

const ComponentTypeDescriptor *ComponentTypeRegistry::find(std::string_view type) const {
    for (const auto &d : m_descriptors)
        if (d.type == type)
            return &d;
    return nullptr;
}

const ComponentTypeDescriptor *
ComponentTypeRegistry::findByProjectType(std::string_view name) const {
    for (const auto &d : m_descriptors)
        if (d.type == name || d.project_type == name)
            return &d;
    return nullptr;
}

std::vector<ComponentTypeDescriptor *> ComponentTypeRegistry::all() {
    std::vector<ComponentTypeDescriptor *> result;
    result.reserve(m_descriptors.size());
    for (auto &d : m_descriptors)
        result.push_back(&d);
    return result;
}

std::string normalizeStatePath(std::string_view path) {
    std::string normalized;
    normalized.reserve(path.size());
    for (size_t index = 0; index < path.size();) {
        if (path[index] == '[') {
            const size_t close = path.find(']', index + 1);
            const std::string_view array_index = close == std::string_view::npos
                                                     ? std::string_view{}
                                                     : path.substr(index + 1, close - index - 1);
            if (!array_index.empty() && std::all_of(array_index.begin(), array_index.end(),
                                                    [](char c) { return c >= '0' && c <= '9'; })) {
                normalized += "[]";
                index = close + 1;
                continue;
            }
        }
        normalized += path[index++];
    }
    return normalized;
}

const ParameterField *findStateField(const ComponentTypeDescriptor &descriptor,
                                     std::string_view path) {
    const std::string key = normalizeStatePath(path);
    for (const auto &field : descriptor.state_fields) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

ComponentTypeRegistry::ComponentTypeRegistry() {
    ComponentTypeDescriptor amp;
    amp.type = "amplifier";
    amp.project_type = "Amplifier";
    amp.display_name = "Amplifier";
    amp.menu_label = "Add Amplifier";
    amp.label_prefix = "Amplifier";
    amp.kind = NodeKind::Amplifier;
    amp.authorable = true;
    amp.supports_sparam_file = true;
    amp.load_sparam_file = &loadSparamFile<AmplifierEngine>;
    amp.fields = {
        {"gain_dB", "Gain", "dB", FieldKind::Number, true, -50.0, 100.0, {}, {}, ""},
        {"nf_dB", "Noise Figure", "dB", FieldKind::Number, false, 0.0, 30.0, {}, {}, ""},
        {"oip2_dBm", "OIP2", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"oip3_dBm", "OIP3", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"p1db_dBm", "P1dB", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
    };
    amp.state_fields = {
        stateField("gain_dB", "Gain", "dB", FieldKind::Number,
                   "Sets the amplifier gain used in ideal mode."),
        stateField("nf_dB", "Noise Figure", "dB", FieldKind::Number,
                   "Sets the amplifier noise figure."),
        stateField("enable_nonlinear", "Enable Nonlinear", "", FieldKind::Bool,
                   "Enables the amplifier's nonlinear model."),
        stateField("oip2_dBm", "OIP2", "dBm", FieldKind::Number,
                   "Sets the amplifier's second-order intercept point."),
        stateField("oip3_dBm", "OIP3", "dBm", FieldKind::Number,
                   "Sets the amplifier's third-order intercept point."),
        stateField("p1db_dBm", "P1dB", "dBm", FieldKind::Number,
                   "Sets the amplifier's one-decibel compression point."),
        stateField("sparam_mode", "S-Parameter Mode", "", FieldKind::Bool,
                   "Uses the loaded S-parameter model instead of ideal gain."),
        stateField("sparam_filepath", "S-Parameter File", "", FieldKind::FilePath,
                   "Identifies the Touchstone file loaded by the component.", {}, true),
        stateField("sparam_fwd_idx", "Forward S-Parameter Index", "", FieldKind::Number,
                   "Selects the forward S-parameter used for signal transfer."),
    };
    amp.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AmplifierEngine>(id, graph));
    };
    m_descriptors.push_back(amp);

    ComponentTypeDescriptor att;
    att.type = "attenuator";
    att.project_type = "Attenuator";
    att.display_name = "Attenuator";
    att.menu_label = "Add Attenuator";
    att.label_prefix = "Attenuator";
    att.kind = NodeKind::Attenuator;
    att.authorable = true;
    att.supports_sparam_file = true;
    att.load_sparam_file = &loadSparamFile<AttenuatorEngine>;
    att.fields = {
        {"attenuation_dB", "Attenuation", "dB", FieldKind::Number, true, 0.0, 100.0, {}, {}, ""},
    };
    att.state_fields = {
        stateField("atten_dB", "Attenuation", "dB", FieldKind::Number,
                   "Sets the ideal attenuation."),
        stateField("sparam_mode", "S-Parameter Mode", "", FieldKind::Bool,
                   "Uses the loaded S-parameter model instead of ideal attenuation."),
        stateField("sparam_filepath", "S-Parameter File", "", FieldKind::FilePath,
                   "Identifies the Touchstone file loaded by the component.", {}, true),
    };
    att.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AttenuatorEngine>(id, graph));
    };
    m_descriptors.push_back(att);

    ComponentTypeDescriptor spl;
    spl.type = "splitter";
    spl.project_type = "Splitter";
    spl.display_name = "Splitter";
    spl.menu_label = "Add Splitter";
    spl.label_prefix = "Splitter";
    spl.kind = NodeKind::Splitter;
    spl.authorable = true;
    spl.state_fields = {};
    spl.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<SplitterEngine>(id, graph));
    };
    m_descriptors.push_back(spl);

    ComponentTypeDescriptor flt;
    flt.type = "filter";
    flt.project_type = "IdealFilter";
    flt.display_name = "IdealFilter";
    flt.menu_label = "Add Ideal Filter";
    flt.label_prefix = "IdealFilter";
    flt.kind = NodeKind::IdealFilter;
    flt.authorable = true;
    flt.supports_sparam_file = true;
    flt.load_sparam_file = &loadSparamFile<IdealFilterEngine>;
    flt.fields = {
        {"filter_type",
         "Filter Type",
         "",
         FieldKind::Enum,
         true,
         0,
         0,
         {"LPF", "HPF", "BPF", "BSF"},
         {},
         ""},
        {"fc_low_Hz", "Low Cutoff", "Hz", FieldKind::Number, false, 0.0, 1e12, {}, {}, ""},
        {"fc_high_Hz", "High Cutoff", "Hz", FieldKind::Number, false, 0.0, 1e12, {}, {}, ""},
    };
    flt.state_fields = {
        stateField("filter_type", "Filter Type", "", FieldKind::Enum,
                   "Selects the ideal filter response.", {"LPF", "HPF", "BPF", "BSF"}),
        stateField("fc_low_Hz", "Low Cutoff", "Hz", FieldKind::Number,
                   "Sets the low cutoff frequency."),
        stateField("fc_high_Hz", "High Cutoff", "Hz", FieldKind::Number,
                   "Sets the high cutoff frequency."),
        stateField("sparam_mode", "S-Parameter Mode", "", FieldKind::Bool,
                   "Uses the loaded S-parameter model instead of the ideal filter."),
        stateField("sparam_filepath", "S-Parameter File", "", FieldKind::FilePath,
                   "Identifies the Touchstone file loaded by the component.", {}, true),
        stateField("sparam_fwd_idx", "Forward S-Parameter Index", "", FieldKind::Number,
                   "Selects the forward S-parameter used for signal transfer."),
    };
    flt.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<IdealFilterEngine>(id, graph));
    };
    m_descriptors.push_back(flt);

    ComponentTypeDescriptor mix;
    mix.type = "mixer";
    mix.project_type = "Mixer";
    mix.display_name = "Mixer";
    mix.menu_label = "Add Mixer";
    mix.label_prefix = "Mixer";
    mix.kind = NodeKind::Mixer;
    mix.authorable = true;
    mix.fields = {
        {"lo_freq_Hz", "LO Frequency", "Hz", FieldKind::Number, true, 0.0, 1e12, {}, {}, ""},
        {"conversion_gain_dB",
         "Conversion Gain",
         "dB",
         FieldKind::Number,
         false,
         -60.0,
         30.0,
         {},
         {},
         ""},
        {"nf_dB", "Noise Figure", "dB", FieldKind::Number, false, 0.0, 30.0, {}, {}, ""},
    };
    mix.state_fields = {
        stateField("lo_freq_Hz", "LO Frequency", "Hz", FieldKind::Number,
                   "Sets the mixer's local-oscillator frequency."),
        stateField("conv_gain_dB", "Conversion Gain", "dB", FieldKind::Number,
                   "Sets the mixer's conversion gain."),
        stateField("nf_dB", "Noise Figure", "dB", FieldKind::Number,
                   "Sets the mixer's noise figure."),
    };
    mix.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<MixerEngine>(id, graph));
    };
    m_descriptors.push_back(mix);

    ComponentTypeDescriptor eq;
    eq.type = "equalizer";
    eq.project_type = "Equalizer";
    eq.display_name = "Equalizer";
    eq.menu_label = "Add Equalizer";
    eq.label_prefix = "Equalizer";
    eq.kind = NodeKind::Equalizer;
    eq.authorable = true;
    eq.supports_sparam_file = true;
    eq.load_sparam_file = &loadSparamFile<EqualizerEngine>;
    eq.fields = {
        {"ref_gain_dB", "Reference Gain", "dB", FieldKind::Number, false, -50.0, 50.0, {}, {}, ""},
        {"ref_freq_Hz",
         "Reference Frequency",
         "Hz",
         FieldKind::Number,
         false,
         0.0,
         1e12,
         {},
         {},
         ""},
        {"slope_dB_per_decade",
         "Slope",
         "dB/decade",
         FieldKind::Number,
         false,
         -100.0,
         100.0,
         {},
         {},
         ""},
    };
    eq.state_fields = {
        stateField("ref_gain_dB", "Reference Gain", "dB", FieldKind::Number,
                   "Sets the equalizer gain at its reference frequency."),
        stateField("ref_freq_Hz", "Reference Frequency", "Hz", FieldKind::Number,
                   "Sets the frequency where the equalizer's reference gain applies."),
        stateField("slope_dB_per_decade", "Slope", "dB/decade", FieldKind::Number,
                   "Sets the equalizer gain change per frequency decade."),
        stateField("sparam_mode", "S-Parameter Mode", "", FieldKind::Bool,
                   "Uses the loaded S-parameter model instead of the ideal equalizer."),
        stateField("sparam_filepath", "S-Parameter File", "", FieldKind::FilePath,
                   "Identifies the Touchstone file loaded by the component.", {}, true),
        stateField("sparam_fwd_idx", "Forward S-Parameter Index", "", FieldKind::Number,
                   "Selects the forward S-parameter used for signal transfer."),
    };
    eq.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<EqualizerEngine>(id, graph));
    };
    m_descriptors.push_back(eq);

    ComponentTypeDescriptor comb;
    comb.type = "combiner";
    comb.project_type = "Combiner";
    comb.display_name = "Combiner";
    comb.menu_label = "Add Combiner";
    comb.label_prefix = "Combiner";
    comb.kind = NodeKind::Combiner;
    comb.authorable = true;
    comb.supports_sparam_file = true;
    comb.load_sparam_file = &loadSparamFile<CombinerEngine>;
    comb.fields = {
        {"manual_mode", "Manual Mode", "", FieldKind::Bool, false, 0, 0, {}, false, ""},
    };
    comb.state_fields = {
        stateField("manual_mode", "Manual Mode", "", FieldKind::Bool,
                   "Enables manual combiner settings."),
        stateField("sparam_mode", "S-Parameter Mode", "", FieldKind::Bool,
                   "Uses the loaded S-parameter model instead of ideal combining."),
        stateField("sparam_filepath", "S-Parameter File", "", FieldKind::FilePath,
                   "Identifies the Touchstone file loaded by the component.", {}, true),
    };
    comb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<CombinerEngine>(id, graph));
    };
    m_descriptors.push_back(comb);

    ComponentTypeDescriptor rfsw;
    rfsw.type = "rf_switch_spdt";
    rfsw.project_type = "RFSwitchSPDT";
    rfsw.display_name = "SPDT Switch";
    rfsw.menu_label = "Add SPDT Switch";
    rfsw.label_prefix = "SPDT Switch";
    rfsw.kind = NodeKind::RFSwitchSPDT;
    rfsw.authorable = true;
    rfsw.supports_sparam_file = false;
    rfsw.fields = {
        {"active_throw", "Active Throw", "", FieldKind::Enum, false, 0, 0, {"T1", "T2"}, "T1", ""},
        {"insertion_loss_dB",
         "Insertion Loss",
         "dB",
         FieldKind::Number,
         true,
         0.0,
         60.0,
         {},
         0.5,
         ""},
        {"isolation_dB", "Isolation", "dB", FieldKind::Number, false, 0.0, 120.0, {}, 40.0, ""},
    };
    rfsw.state_fields = {
        stateField("active_throw", "Active Throw", "", FieldKind::Enum,
                   "Selects the active switch throw.", {"T1", "T2"}),
        stateField("insertion_loss_dB", "Insertion Loss", "dB", FieldKind::Number,
                   "Sets the selected throw's insertion loss."),
        stateField("isolation_dB", "Isolation", "dB", FieldKind::Number,
                   "Sets the isolation between switch throws."),
    };
    rfsw.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<RFSwitchEngine>(id, graph));
    };
    m_descriptors.push_back(rfsw);

    ComponentTypeDescriptor rfsw2;
    rfsw2.type = "rf_switch_spdt_2to1";
    rfsw2.project_type = "RFSwitchSPDT2to1";
    rfsw2.display_name = "SPDT Switch (2:1)";
    rfsw2.menu_label = "Add SPDT Switch (2:1)";
    rfsw2.label_prefix = "SPDT Switch (2:1)";
    rfsw2.kind = NodeKind::RFSwitchSPDT2to1;
    rfsw2.authorable = true;
    rfsw2.supports_sparam_file = false;
    rfsw2.fields = {
        {"active_throw", "Active Throw", "", FieldKind::Enum, false, 0, 0, {"T1", "T2"}, "T1", ""},
        {"insertion_loss_dB",
         "Insertion Loss",
         "dB",
         FieldKind::Number,
         true,
         0.0,
         60.0,
         {},
         0.5,
         ""},
        {"isolation_dB", "Isolation", "dB", FieldKind::Number, false, 0.0, 120.0, {}, 40.0, ""},
    };
    rfsw2.state_fields = {
        stateField("active_throw", "Active Throw", "", FieldKind::Enum,
                   "Selects the active switch throw.", {"T1", "T2"}),
        stateField("insertion_loss_dB", "Insertion Loss", "dB", FieldKind::Number,
                   "Sets the selected throw's insertion loss."),
        stateField("isolation_dB", "Isolation", "dB", FieldKind::Number,
                   "Sets the isolation between switch throws."),
    };
    rfsw2.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<RFSwitch2to1Engine>(id, graph));
    };
    m_descriptors.push_back(rfsw2);

    ComponentTypeDescriptor adc;
    adc.type = "adc";
    adc.project_type = "ADC";
    adc.display_name = "ADC";
    adc.menu_label = "Add RF ADC";
    adc.label_prefix = "ADC";
    adc.kind = NodeKind::Adc;
    adc.authorable = true;
    adc.fields = {
        {"fs_Hz", "Sample Rate", "Hz", FieldKind::Number, true, 0.0, 1e12, {}, {}, ""},
        {"nsd_dBm_per_Hz",
         "Noise Spectral Density",
         "dBm/Hz",
         FieldKind::Number,
         false,
         -200.0,
         0.0,
         {},
         {},
         ""},
        {"decimation", "DDC Decimation", "", FieldKind::Number, false, 1.0, 8.0, {}, {}, ""},
        {"nco_fs_fraction", "NCO (×Fs)", "", FieldKind::Number, false, -0.5, 0.5, {}, {}, ""},
    };
    adc.state_fields = {
        stateField("sample_rate_Hz", "Sample Rate", "Hz", FieldKind::Number,
                   "Sets the ADC input sample rate."),
        stateField("nsd_dBm_per_Hz", "Noise Spectral Density", "dBm/Hz", FieldKind::Number,
                   "Sets the ADC input noise spectral density."),
        stateField("decimation", "DDC Decimation", "", FieldKind::Number,
                   "Sets the ADC digital down-converter decimation factor."),
        stateField("nco_fs_fraction", "NCO (×Fs)", "×Fs", FieldKind::Number,
                   "Sets NCO tuning as a fraction of the ADC sample rate."),
    };
    adc.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AdcEngine>(id, graph));
    };
    m_descriptors.push_back(adc);

    ComponentTypeDescriptor gen;
    gen.type = "generator";
    gen.project_type = "SignalGenerator";
    gen.display_name = "Generator";
    gen.menu_label = "Add Generator";
    gen.label_prefix = "Generator";
    gen.kind = NodeKind::Generator;
    gen.state_fields = {
        stateField("tones[].freq_Hz", "Tone Frequency", "Hz", FieldKind::Number,
                   "Sets a generator tone frequency."),
        stateField("tones[].power_dBm", "Tone Power", "dBm", FieldKind::Number,
                   "Sets a generator tone power."),
        stateField("tones[].phase_deg", "Tone Phase", "deg", FieldKind::Number,
                   "Sets a generator tone phase."),
        stateField("fs_Hz", "Sample Rate", "Hz", FieldKind::Number,
                   "Sets the generator's output sample rate."),
    };
    gen.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<SignalGeneratorEngine>(id, graph));
    };
    m_descriptors.push_back(gen);

    ComponentTypeDescriptor coax;
    coax.type = "coax";
    coax.project_type = "CoaxCable";
    coax.display_name = "Coax Cable";
    coax.menu_label = "Add Coax Cable";
    coax.label_prefix = "Coax Cable";
    coax.kind = NodeKind::CoaxCable;
    std::vector<std::string> preset_labels;
    for (const auto &preset : kCoaxCablePresets)
        preset_labels.emplace_back(preset.name);
    coax.state_fields = {
        stateField("preset_index", "Cable Preset", "", FieldKind::Enum,
                   "Selects the cable model used for attenuation and delay.",
                   std::move(preset_labels)),
        stateField("length_m", "Length", "m", FieldKind::Number, "Sets the physical cable length."),
        stateField("connectors_loss_dB", "Connector Loss", "dB", FieldKind::Number,
                   "Sets the total loss of the cable connectors."),
    };
    coax.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<CoaxCableEngine>(id, graph));
    };
    m_descriptors.push_back(coax);

    ComponentTypeDescriptor pfb;
    pfb.type = "pfb";
    pfb.project_type = "PFBChannelizer";
    pfb.display_name = "PFB";
    pfb.menu_label = "Add PFB Channelizer";
    pfb.label_prefix = "PFB";
    pfb.kind = NodeKind::PFB;
    pfb.state_fields = {
        stateField("channel_count", "Channel Count", "", FieldKind::Number,
                   "Sets the number of PFB output channels."),
        stateField("taps_per_branch", "Taps per Branch", "", FieldKind::Number,
                   "Sets the FIR tap count in each PFB branch."),
        stateField("kaiser_beta", "Kaiser Beta", "", FieldKind::Number,
                   "Sets the Kaiser window beta parameter."),
        stateField("sampling_ratio", "Sampling Ratio", "", FieldKind::Number,
                   "Selects critical or two-times PFB sampling."),
        stateField("active_channel", "Active Channel", "", FieldKind::Number,
                   "Selects the PFB channel used by active-channel views."),
    };
    pfb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<PFBChannelizerEngine>(id, graph));
    };
    m_descriptors.push_back(pfb);
}
