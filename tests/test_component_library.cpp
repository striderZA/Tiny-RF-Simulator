#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "combiner_engine.h"
#include "component_library.h"
#include "component_registry.h"
#include "equalizer_engine.h"
#include "ideal_filter_engine.h"
#include "mixer_engine.h"
#include "splitter_engine.h"
#include "view_manager.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>

using Catch::Approx;

static std::string write_temp_json(const std::string &content) {
    static std::random_device rd;
    std::uniform_int_distribution<int> dis(100000, 999999);
    auto path = std::filesystem::temp_directory_path() /
                ("test_component_" + std::to_string(dis(rd)) + ".json");
    std::ofstream ofs(path);
    ofs << content;
    ofs.close();
    return path.string();
}

TEST_CASE("ComponentLibrary loads valid amplifier JSON", "[library]") {
    std::string json = R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "TEST-AMP-001",
        "manufacturer": "Test Corp",
        "description": "Test amplifier",
        "parameters": {
            "gain_dB": 20.0,
            "nf_dB": 2.5,
            "oip3_dBm": 30.0,
            "p1db_dBm": 15.0
        }
    })";

    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0]->type == "amplifier");
    REQUIRE(defs[0]->part_number == "TEST-AMP-001");
    REQUIRE(defs[0]->manufacturer == "Test Corp");
    REQUIRE(defs[0]->parameters["gain_dB"].get<double>() == Approx(20.0));
    REQUIRE(defs[0]->parameters["nf_dB"].get<double>() == Approx(2.5));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary scans directory recursively", "[library]") {
    std::filesystem::create_directories("test_lib/amplifiers/test");

    std::string json1 =
        R"({"schema_version":1,"type":"amplifier","part_number":"AMP-001","parameters":{"gain_dB":10.0}})";
    std::string json2 =
        R"({"schema_version":1,"type":"amplifier","part_number":"AMP-002","parameters":{"gain_dB":20.0}})";

    {
        std::ofstream ofs("test_lib/amplifiers/test/amp1.json");
        ofs << json1;
    }
    {
        std::ofstream ofs("test_lib/amplifiers/test/amp2.json");
        ofs << json2;
    }

    ComponentLibrary lib;
    lib.scan("test_lib");

    auto defs = lib.all();
    REQUIRE(defs.size() == 2);

    auto amps = lib.byType("amplifier");
    REQUIRE(amps.size() == 2);

    std::filesystem::remove_all("test_lib");
}

TEST_CASE("ComponentLibrary instantiates amplifier from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"amplifier","part_number":"TEST-AMP","parameters":{"gain_dB":25.0,"nf_dB":3.0,"oip3_dBm":35.0,"p1db_dBm":20.0}})";

    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 100, registry, graph);
    REQUIRE(engine != nullptr);

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    REQUIRE(amp->gain_dB() == Approx(25.0));
    REQUIRE(amp->nf_dB() == Approx(3.0));
    REQUIRE(amp->oip3_dBm() == Approx(35.0));
    REQUIRE(amp->p1db_dBm() == Approx(20.0));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates attenuator from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"attenuator","part_number":"TEST-ATT","parameters":{"attenuation_dB":10.0}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 101, registry, graph);
    REQUIRE(engine != nullptr);
    auto *att = dynamic_cast<AttenuatorEngine *>(engine);
    REQUIRE(att != nullptr);
    REQUIRE(att->attenuation() == Approx(10.0));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates splitter from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"splitter","part_number":"TEST-SPL","parameters":{}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 102, registry, graph);
    REQUIRE(engine != nullptr);
    auto *spl = dynamic_cast<SplitterEngine *>(engine);
    REQUIRE(spl != nullptr);

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates filter from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"filter","part_number":"TEST-FLT","parameters":{"filter_type":"BPF","fc_low_Hz":100e6,"fc_high_Hz":200e6}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 103, registry, graph);
    REQUIRE(engine != nullptr);
    auto *flt = dynamic_cast<IdealFilterEngine *>(engine);
    REQUIRE(flt != nullptr);
    REQUIRE(flt->filterType() == FilterType::BPF);
    REQUIRE(flt->fcLow_Hz() == Approx(100e6));
    REQUIRE(flt->fcHigh_Hz() == Approx(200e6));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates mixer from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"mixer","part_number":"TEST-MIX","parameters":{"lo_freq_Hz":500e6,"conversion_gain_dB":-7.0,"nf_dB":8.0}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 104, registry, graph);
    REQUIRE(engine != nullptr);
    auto *mix = dynamic_cast<MixerEngine *>(engine);
    REQUIRE(mix != nullptr);
    REQUIRE(mix->loFreq_Hz() == Approx(500e6));
    REQUIRE(mix->conversionGain_dB() == Approx(-7.0));
    REQUIRE(mix->nf_dB() == Approx(8.0));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates equalizer from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"equalizer","part_number":"TEST-EQ","parameters":{"ref_gain_dB":0.0,"ref_freq_Hz":1e9,"slope_dB_per_decade":-12.0}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 105, registry, graph);
    REQUIRE(engine != nullptr);
    auto *eq = dynamic_cast<EqualizerEngine *>(engine);
    REQUIRE(eq != nullptr);
    REQUIRE(eq->refGain_dB() == Approx(0.0));
    REQUIRE(eq->refFreq_Hz() == Approx(1e9));
    REQUIRE(eq->slope_dBPerDecade() == Approx(-12.0));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates combiner from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"combiner","part_number":"TEST-COMB","parameters":{"manual_mode":true}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 106, registry, graph);
    REQUIRE(engine != nullptr);
    auto *comb = dynamic_cast<CombinerEngine *>(engine);
    REQUIRE(comb != nullptr);
    REQUIRE(comb->manualMode() == true);

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates adc from definition", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"adc","part_number":"TEST-ADC","parameters":{"fs_Hz":65e6,"nsd_dBm_per_Hz":-153.0}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 107, registry, graph);
    REQUIRE(engine != nullptr);
    auto *adc = dynamic_cast<AdcEngine *>(engine);
    REQUIRE(adc != nullptr);
    REQUIRE(adc->fs_Hz() == Approx(65e6));
    REQUIRE(adc->nsd_dBm_per_Hz() == Approx(-153.0));

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary sets part_number on graph node", "[library]") {
    std::string json =
        R"({"schema_version":1,"type":"amplifier","part_number":"ZX60-33LN+","manufacturer":"Mini-Circuits","parameters":{"gain_dB":28.0,"nf_dB":1.1}})";
    auto path = write_temp_json(json);

    ComponentLibrary lib;
    lib.loadFile(path);
    auto defs = lib.all();
    REQUIRE(defs.size() == 1);
    REQUIRE(defs[0]->part_number == "ZX60-33LN+");

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 200, registry, graph);
    REQUIRE(engine != nullptr);

    // Verify part_number was set on the graph node
    bool found = false;
    for (const auto &gn : graph.nodes()) {
        if (gn.node_id == engine->graphNodeId()) {
            REQUIRE(gn.part_number == "ZX60-33LN+");
            found = true;
            break;
        }
    }
    REQUIRE(found);

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary parses data_files array from v2 JSON", "[library]") {
    std::string json = R"({
        "schema_version": 2,
        "type": "amplifier",
        "part_number": "TEST-DATA-001",
        "manufacturer": "Test Corp",
        "parameters": {
            "gain_dB": 20.0,
            "nf_dB": 1.0
        },
        "data_files": [
            {"type": "s_parameters", "path": "TEST001.s2p"}
        ]
    })";

    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    const auto &def = *defs[0];
    REQUIRE(def.schema_version == 2);
    REQUIRE(def.part_number == "TEST-DATA-001");
    REQUIRE(def.data_files.size() == 1);
    REQUIRE(def.data_files[0].type == "s_parameters");
    REQUIRE(def.data_files[0].path == "TEST001.s2p");

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary handles v1 JSON without data_files", "[library]") {
    std::string json = R"({
        "schema_version": 1,
        "type": "amplifier",
        "part_number": "V1PART",
        "parameters": {
            "gain_dB": 15.0
        }
    })";

    auto path = write_temp_json(json);
    ComponentLibrary lib;
    lib.loadFile(path);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    const auto &def = *defs[0];
    REQUIRE(def.schema_version == 1);
    REQUIRE(def.data_files.empty());

    std::filesystem::remove(path);
}

TEST_CASE("ComponentLibrary instantiates amplifier with S-param file", "[library]") {
    // Create a temporary directory with JSON and S-param file
    std::string tmpdir = (std::filesystem::temp_directory_path() / "sparam_test").string();
    std::filesystem::create_directories(tmpdir);

    std::string sparam_path_str = tmpdir + "/amp.s2p";
    std::string json_path_str = tmpdir + "/amp_with_sparam.json";

    // Create minimal valid .s2p file
    {
        std::ofstream ofs(sparam_path_str);
        ofs << "! Test S-param file\n";
        ofs << "# GHz S MA R 50\n";
        ofs << "1.0 0.5 0.0 2.0 90.0 0.1 180.0 0.3 -45.0\n";
    }

    // Create JSON referencing the S-param file
    std::string json = R"({
        "schema_version": 2,
        "type": "amplifier",
        "part_number": "AMP_SPARAM",
        "parameters": {
            "gain_dB": 20.0,
            "nf_dB": 1.0
        },
        "data_files": [
            {"type": "s_parameters", "path": "amp.s2p"}
        ]
    })";
    {
        std::ofstream ofs(json_path_str);
        ofs << json;
    }

    ComponentLibrary lib;
    lib.loadFile(json_path_str);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 300, registry, graph);
    REQUIRE(engine != nullptr);

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    REQUIRE(amp->sparamMode() == true);
    REQUIRE(amp->sparamLoaded() == true);

    std::filesystem::remove_all(tmpdir);
}

TEST_CASE("ComponentLibrary falls back when S-param file missing", "[library]") {
    std::string tmpdir = (std::filesystem::temp_directory_path() / "fallback_test").string();
    std::filesystem::create_directories(tmpdir);

    std::string json_path_str = tmpdir + "/amp_missing_sparam.json";

    // Create JSON referencing non-existent S-param file
    std::string json = R"({
        "schema_version": 2,
        "type": "amplifier",
        "part_number": "AMP_MISSING",
        "parameters": {
            "gain_dB": 20.0,
            "nf_dB": 1.0
        },
        "data_files": [
            {"type": "s_parameters", "path": "nonexistent.s2p"}
        ]
    })";
    {
        std::ofstream ofs(json_path_str);
        ofs << json;
    }

    ComponentLibrary lib;
    lib.loadFile(json_path_str);

    auto defs = lib.all();
    REQUIRE(defs.size() == 1);

    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry(graph, view);

    auto *engine = lib.instantiate(*defs[0], 301, registry, graph);
    REQUIRE(engine != nullptr);

    auto *amp = dynamic_cast<AmplifierEngine *>(engine);
    REQUIRE(amp != nullptr);
    REQUIRE(amp->sparamMode() == false);     // Should NOT be in S-param mode
    REQUIRE(amp->gain_dB() == Approx(20.0)); // Should use single-point params
    REQUIRE(amp->nf_dB() == Approx(1.0));

    std::filesystem::remove_all(tmpdir);
}