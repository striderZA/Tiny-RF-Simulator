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
#include "signal_generator_engine.h"
#include "splitter_engine.h"

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
    amp.fields = {
        {"gain_dB", "Gain", "dB", FieldKind::Number, true, -50.0, 100.0, {}, {}, ""},
        {"nf_dB", "Noise Figure", "dB", FieldKind::Number, false, 0.0, 30.0, {}, {}, ""},
        {"oip2_dBm", "OIP2", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"oip3_dBm", "OIP3", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"p1db_dBm", "P1dB", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
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
    att.fields = {
        {"attenuation_dB", "Attenuation", "dB", FieldKind::Number, true, 0.0, 100.0, {}, {}, ""},
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
    comb.fields = {
        {"manual_mode", "Manual Mode", "", FieldKind::Bool, false, 0, 0, {}, false, ""},
    };
    comb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<CombinerEngine>(id, graph));
    };
    m_descriptors.push_back(comb);

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
    pfb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<PFBChannelizerEngine>(id, graph));
    };
    m_descriptors.push_back(pfb);
}
