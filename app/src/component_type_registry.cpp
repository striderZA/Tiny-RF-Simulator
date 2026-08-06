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
    // Legacy factory: applied library params directly. Removed in Task 2b.
    amp.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AmplifierEngine>(id, graph);
        if (parameters.contains("gain_dB"))
            e.setGain_dB(parameters["gain_dB"].get<double>());
        if (parameters.contains("nf_dB"))
            e.setNF_dB(parameters["nf_dB"].get<double>());
        if (parameters.contains("oip2_dBm"))
            e.setOIP2_dBm(parameters["oip2_dBm"].get<double>());
        if (parameters.contains("oip3_dBm"))
            e.setOIP3_dBm(parameters["oip3_dBm"].get<double>());
        if (parameters.contains("p1db_dBm"))
            e.setP1dB_dBm(parameters["p1db_dBm"].get<double>());
        bool has_nonlinear = parameters.contains("oip2_dBm") || parameters.contains("oip3_dBm") ||
                             parameters.contains("p1db_dBm");
        if (has_nonlinear)
            e.setEnableNonlinear(true);
        return &e;
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
    att.fields = {
        {"attenuation_dB", "Attenuation", "dB", FieldKind::Number, true, 0.0, 100.0, {}, {}, ""},
    };
    att.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AttenuatorEngine>(id, graph));
    };
    att.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AttenuatorEngine>(id, graph);
        if (parameters.contains("attenuation_dB"))
            e.setAttenuation(parameters["attenuation_dB"].get<double>());
        return &e;
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
    spl.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &) -> IComponentEngine * {
        auto &e = registry.add<SplitterEngine>(id, graph);
        return &e;
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
    flt.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<IdealFilterEngine>(id, graph);
        if (parameters.contains("filter_type")) {
            std::string ft = parameters["filter_type"].get<std::string>();
            if (ft == "LPF")
                e.setFilterType(FilterType::LPF);
            else if (ft == "HPF")
                e.setFilterType(FilterType::HPF);
            else if (ft == "BPF")
                e.setFilterType(FilterType::BPF);
            else if (ft == "BSF")
                e.setFilterType(FilterType::BSF);
        }
        double fc_low = parameters.value("fc_low_Hz", 100e6);
        double fc_high = parameters.value("fc_high_Hz", 200e6);
        if (parameters.contains("fc_low_Hz") && parameters.contains("fc_high_Hz"))
            e.setCutoffs_Hz(fc_low, fc_high);
        else if (parameters.contains("fc_low_Hz"))
            e.setCutoff_Hz(fc_low);
        return &e;
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
    mix.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<MixerEngine>(id, graph);
        if (parameters.contains("lo_freq_Hz"))
            e.setLoFreq_Hz(parameters["lo_freq_Hz"].get<double>());
        if (parameters.contains("conversion_gain_dB"))
            e.setConversionGain_dB(parameters["conversion_gain_dB"].get<double>());
        if (parameters.contains("nf_dB"))
            e.setNF_dB(parameters["nf_dB"].get<double>());
        return &e;
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
    eq.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                    const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<EqualizerEngine>(id, graph);
        if (parameters.contains("ref_gain_dB"))
            e.setRefGain_dB(parameters["ref_gain_dB"].get<double>());
        if (parameters.contains("ref_freq_Hz"))
            e.setRefFreq_Hz(parameters["ref_freq_Hz"].get<double>());
        if (parameters.contains("slope_dB_per_decade"))
            e.setSlope_dBPerDecade(parameters["slope_dB_per_decade"].get<double>());
        return &e;
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
    comb.fields = {
        {"manual_mode", "Manual Mode", "", FieldKind::Bool, false, 0, 0, {}, false, ""},
    };
    comb.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<CombinerEngine>(id, graph));
    };
    comb.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                      const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<CombinerEngine>(id, graph);
        if (parameters.contains("manual_mode"))
            e.setManualMode(parameters["manual_mode"].get<bool>());
        return &e;
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
    };
    adc.create = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id) {
        return static_cast<IComponentEngine *>(&registry.add<AdcEngine>(id, graph));
    };
    adc.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AdcEngine>(id, graph);
        if (parameters.contains("fs_Hz"))
            e.setFs_Hz(parameters["fs_Hz"].get<double>());
        if (parameters.contains("nsd_dBm_per_Hz"))
            e.setNsd_dBm_per_Hz(parameters["nsd_dBm_per_Hz"].get<double>());
        return &e;
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
