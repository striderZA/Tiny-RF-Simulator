// app/src/component_type_registry.cpp
#include "component_type_registry.h"

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "combiner_engine.h"
#include "component_registry.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "splitter_engine.h"

const ComponentTypeRegistry &ComponentTypeRegistry::instance() {
    static ComponentTypeRegistry reg;
    return reg;
}

const ComponentTypeDescriptor *ComponentTypeRegistry::find(const std::string &type) const {
    for (const auto &d : m_descriptors)
        if (d.type == type)
            return &d;
    return nullptr;
}

std::vector<const ComponentTypeDescriptor *> ComponentTypeRegistry::all() const {
    std::vector<const ComponentTypeDescriptor *> result;
    result.reserve(m_descriptors.size());
    for (const auto &d : m_descriptors)
        result.push_back(&d);
    return result;
}

ComponentTypeRegistry::ComponentTypeRegistry() {
    ComponentTypeDescriptor amp;
    amp.type = "amplifier";
    amp.display_name = "Amplifier";
    amp.supports_sparam_file = true;
    amp.fields = {
        {"gain_dB", "Gain", "dB", FieldKind::Number, true, -50.0, 100.0, {}, {}, ""},
        {"nf_dB", "Noise Figure", "dB", FieldKind::Number, false, 0.0, 30.0, {}, {}, ""},
        {"oip2_dBm", "OIP2", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"oip3_dBm", "OIP3", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
        {"p1db_dBm", "P1dB", "dBm", FieldKind::Number, false, -20.0, 100.0, {}, {}, ""},
    };
    amp.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &parameters) -> IComponentEngine * {
        auto &e = registry.add<AmplifierEngine>(id, graph);
        if (parameters.contains("gain_dB"))
            e.setGain_dB(parameters["gain_dB"].get<double>());
        if (parameters.contains("nf_db_vs_freq"))
            e.setNfCurve(parameters["nf_db_vs_freq"]);
        else if (parameters.contains("nf_dB"))
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
    att.display_name = "Attenuator";
    att.fields = {
        {"attenuation_dB", "Attenuation", "dB", FieldKind::Number, true, 0.0, 100.0, {}, {}, ""},
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
    spl.display_name = "Splitter";
    spl.factory = [](ComponentRegistry &registry, NodeGraphEngine &graph, int id,
                     const nlohmann::json &) -> IComponentEngine * {
        auto &e = registry.add<SplitterEngine>(id, graph);
        return &e;
    };
    m_descriptors.push_back(spl);

    ComponentTypeDescriptor flt;
    flt.type = "filter";
    flt.display_name = "Filter";
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
    mix.display_name = "Mixer";
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
    eq.display_name = "Equalizer";
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
    comb.display_name = "Combiner";
    comb.fields = {
        {"manual_mode", "Manual Mode", "", FieldKind::Bool, false, 0, 0, {}, false, ""},
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
    adc.display_name = "ADC";
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
}
