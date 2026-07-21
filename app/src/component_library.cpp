#include "component_library.h"
#include "logging_core.h"
#include <filesystem>

#include "component_registry.h"
#include "component_interface.h"
#include "node_graph_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "splitter_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "equalizer_engine.h"
#include "combiner_engine.h"
#include "adc_engine.h"
#include <fstream>

void ComponentLibrary::loadFile(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        LOG_WARN("ComponentLibrary: cannot open file: %s", filepath.c_str());
        return;
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const nlohmann::json::parse_error& e) {
        LOG_WARN("ComponentLibrary: JSON parse error in %s: %s", filepath.c_str(), e.what());
        return;
    }

    if (!j.contains("type") || !j.contains("part_number") || !j.contains("parameters")) {
        LOG_WARN("ComponentLibrary: missing required fields in %s", filepath.c_str());
        return;
    }

    ComponentDefinition def;
    def.schema_version = j.value("schema_version", 1);
    def.type = j["type"].get<std::string>();
    def.part_number = j["part_number"].get<std::string>();
    def.manufacturer = j.value("manufacturer", "");
    def.description = j.value("description", "");
    def.parameters = j["parameters"];
    def.test_conditions = j.value("test_conditions", nlohmann::json::object());
    def.notes = j.value("notes", "");
    def.source_path = filepath;

    // Parse data_files array if present
    if (j.contains("data_files") && j["data_files"].is_array()) {
        for (const auto& df : j["data_files"]) {
            if (df.contains("type") && df.contains("path")) {
                def.data_files.push_back({
                    df["type"].get<std::string>(),
                    df["path"].get<std::string>()
                });
            }
        }
    }

    m_definitions.push_back(std::move(def));
}

std::vector<const ComponentDefinition*> ComponentLibrary::all() const {
    std::vector<const ComponentDefinition*> result;
    result.reserve(m_definitions.size());
    for (const auto& def : m_definitions) {
        result.push_back(&def);
    }
    return result;
}

void ComponentLibrary::scan(const std::string& directory) {
    namespace fs = std::filesystem;
    if (!fs::exists(directory)) return;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            loadFile(entry.path().string());
        }
    }
}

std::vector<const ComponentDefinition*> ComponentLibrary::byType(const std::string& type) const {
    std::vector<const ComponentDefinition*> result;
    for (const auto& def : m_definitions) {
        if (def.type == type) result.push_back(&def);
    }
    return result;
}

IComponentEngine* ComponentLibrary::instantiate(const ComponentDefinition& def,
                                                 int id,
                                                 ComponentRegistry& registry,
                                                 NodeGraphEngine& graph) {
    IComponentEngine* result = nullptr;

    if (def.type == "amplifier") {
        auto& amp = registry.add<AmplifierEngine>(id, graph);
        if (def.parameters.contains("gain_dB"))
            amp.setGain_dB(def.parameters["gain_dB"].get<double>());
        if (def.parameters.contains("nf_dB"))
            amp.setNF_dB(def.parameters["nf_dB"].get<double>());
        if (def.parameters.contains("oip2_dBm"))
            amp.setOIP2_dBm(def.parameters["oip2_dBm"].get<double>());
        if (def.parameters.contains("oip3_dBm"))
            amp.setOIP3_dBm(def.parameters["oip3_dBm"].get<double>());
        if (def.parameters.contains("p1db_dBm"))
            amp.setP1dB_dBm(def.parameters["p1db_dBm"].get<double>());
        bool has_nonlinear = def.parameters.contains("oip2_dBm") ||
                            def.parameters.contains("oip3_dBm") ||
                            def.parameters.contains("p1db_dBm");
        if (has_nonlinear) amp.setEnableNonlinear(true);
        result = &amp;
    } else if (def.type == "attenuator") {
        auto& att = registry.add<AttenuatorEngine>(id, graph);
        if (def.parameters.contains("attenuation_dB"))
            att.setAttenuation(def.parameters["attenuation_dB"].get<double>());
        result = &att;
    } else if (def.type == "splitter") {
        auto& spl = registry.add<SplitterEngine>(id, graph);
        result = &spl;
    } else if (def.type == "filter") {
        auto& flt = registry.add<IdealFilterEngine>(id, graph);
        if (def.parameters.contains("filter_type")) {
            std::string ft = def.parameters["filter_type"].get<std::string>();
            if (ft == "LPF") flt.setFilterType(FilterType::LPF);
            else if (ft == "HPF") flt.setFilterType(FilterType::HPF);
            else if (ft == "BPF") flt.setFilterType(FilterType::BPF);
            else if (ft == "BSF") flt.setFilterType(FilterType::BSF);
        }
        double fc_low = def.parameters.value("fc_low_Hz", 100e6);
        double fc_high = def.parameters.value("fc_high_Hz", 200e6);
        if (def.parameters.contains("fc_low_Hz") && def.parameters.contains("fc_high_Hz"))
            flt.setCutoffs_Hz(fc_low, fc_high);
        else if (def.parameters.contains("fc_low_Hz"))
            flt.setCutoff_Hz(fc_low);
        result = &flt;
    } else if (def.type == "mixer") {
        auto& mix = registry.add<MixerEngine>(id, graph);
        if (def.parameters.contains("lo_freq_Hz"))
            mix.setLoFreq_Hz(def.parameters["lo_freq_Hz"].get<double>());
        if (def.parameters.contains("conversion_gain_dB"))
            mix.setConversionGain_dB(def.parameters["conversion_gain_dB"].get<double>());
        if (def.parameters.contains("nf_dB"))
            mix.setNF_dB(def.parameters["nf_dB"].get<double>());
        result = &mix;
    } else if (def.type == "equalizer") {
        auto& eq = registry.add<EqualizerEngine>(id, graph);
        if (def.parameters.contains("ref_gain_dB"))
            eq.setRefGain_dB(def.parameters["ref_gain_dB"].get<double>());
        if (def.parameters.contains("ref_freq_Hz"))
            eq.setRefFreq_Hz(def.parameters["ref_freq_Hz"].get<double>());
        if (def.parameters.contains("slope_dB_per_decade"))
            eq.setSlope_dBPerDecade(def.parameters["slope_dB_per_decade"].get<double>());
        result = &eq;
    } else if (def.type == "combiner") {
        auto& comb = registry.add<CombinerEngine>(id, graph);
        if (def.parameters.contains("manual_mode"))
            comb.setManualMode(def.parameters["manual_mode"].get<bool>());
        result = &comb;
    } else if (def.type == "adc") {
        auto& adc = registry.add<AdcEngine>(id, graph);
        if (def.parameters.contains("fs_Hz"))
            adc.setFs_Hz(def.parameters["fs_Hz"].get<double>());
        if (def.parameters.contains("nsd_dBm_per_Hz"))
            adc.setNsd_dBm_per_Hz(def.parameters["nsd_dBm_per_Hz"].get<double>());
        result = &adc;
    } else {
        LOG_WARN("ComponentLibrary: unknown component type '%s'", def.type.c_str());
        return nullptr;
    }

    if (result && !def.part_number.empty())
        graph.setNodePartNumber(result->graphNodeId(), def.part_number);
    return result;
}
