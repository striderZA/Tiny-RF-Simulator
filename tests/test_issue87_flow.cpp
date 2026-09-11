#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "flow_metrics.h"
#include "flow_params.h"
#include "flow_result.h"
#include "flow_runner.h"
#include "node_graph_engine.h"
#include "pfb_channelizer_engine.h"
#include "signal_generator_engine.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

class ThrowingEngine final : public IComponentEngine {
  public:
    ThrowingEngine(int id, NodeGraphEngine &graph, bool fail_on_zero = false)
        : m_id(id), m_fail_on_zero(fail_on_zero), m_graph(&graph) {
        m_graph_node_id = graph.addNode("Throwing", &m_node, 0, 1);
        m_node.outputs.resize(1);
    }

    int id() const override { return m_id; }
    int graphNodeId() const override { return m_graph_node_id; }
    int outputPinId() const override { return m_graph->outputPinId(m_graph_node_id); }
    std::string hoverSummary() const override { return "Throwing"; }
    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }
    void update(double) override {}
    std::string_view type_name() const override { return "throwing"; }

    nlohmann::json serialize() const override { return {{"value", m_value}}; }
    void deserialize(const nlohmann::json &snapshot) override {
        const double value = snapshot.at("value").get<double>();
        m_value = value;
        if (value != 0.0 || (m_fail_on_zero && value == 0.0))
            throw std::runtime_error("intentional deserialize failure");
    }

  private:
    int m_id;
    int m_graph_node_id = -1;
    double m_value = 0.0;
    bool m_fail_on_zero = false;
    SignalNode m_node;
    NodeGraphEngine *m_graph = nullptr;
};

using Catch::Approx;

namespace {
const MetricDefinition *metric(const std::string &name) {
    return MetricRegistry::instance().find(name);
}
} // namespace

TEST_CASE("issue87 metrics: power_dBm integrates noise density over the bin width",
          "[issue87][metrics]") {
    // No tones: the noise term is the only contribution, so this constrains
    // density * bin_width rather than merely the dBm conversion.
    Spectrum spec;
    spec.frequencies = {1e9, 1.1e9}; // 1e8 Hz bins
    spec.noise_total_W = {4.0e-21, 4.0e-21};
    // 2 bins * 4e-21 W/Hz * 1e8 Hz = 8e-13 W -> 10*log10(8e-10) = -90.96910 dBm
    const MetricDefinition *def = metric("power_dBm");
    REQUIRE(def != nullptr);
    REQUIRE(def->compute(spec) == Approx(-90.96910).margin(1e-4));
}

TEST_CASE("issue87 metrics: power_dBm scales with the bin width", "[issue87][metrics]") {
    Spectrum narrow;
    narrow.frequencies = {1e9, 1.1e9}; // 1e8 Hz bins
    narrow.noise_total_W = {4.0e-21, 4.0e-21};
    Spectrum wide;
    wide.frequencies = {1e9, 1.2e9}; // 2e8 Hz bins
    wide.noise_total_W = {4.0e-21, 4.0e-21};

    const MetricDefinition *def = metric("power_dBm");
    REQUIRE(def != nullptr);
    // Doubling the bin width doubles the integrated noise: +10*log10(2) dB.
    REQUIRE(def->compute(wide) - def->compute(narrow) == Approx(3.0103).margin(1e-3));
}

TEST_CASE("issue87 metrics: power_dBm sums a tone and integrated noise", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 1.1e9};
    spec.tones = {{1e9, -30.0, 0.0}};
    spec.noise_total_W = {4.0e-21, 4.0e-21};

    const MetricDefinition *def = metric("power_dBm");
    REQUIRE(def != nullptr);
    REQUIRE(def->unit == "dBm");
    // 1e-6 W tone + 8e-13 W noise = 1.0000008e-6 W -> -29.99999653 dBm
    REQUIRE(def->compute(spec) == Approx(-29.99999653).margin(1e-6));
}

TEST_CASE("issue87 metrics: a silent spectrum is a valid -inf power measurement",
          "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 1.1e9};
    spec.noise_total_W = {0.0, 0.0};

    const MetricDefinition *def = metric("power_dBm");
    REQUIRE(def != nullptr);
    const double value = def->compute(spec);
    REQUIRE(std::isinf(value));
    REQUIRE(value < 0.0);
}

TEST_CASE("issue87 metrics: peak metrics pick the strongest tone", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};
    spec.tones = {{1e9, -40.0, 0.0}, {2e9, -10.0, 0.0}, {3e9, -20.0, 0.0}};

    const MetricDefinition *power = metric("peak_power_dBm");
    const MetricDefinition *freq = metric("peak_freq_Hz");
    REQUIRE(power != nullptr);
    REQUIRE(freq != nullptr);
    REQUIRE(power->compute(spec) == Approx(-10.0));
    REQUIRE(freq->compute(spec) == Approx(2e9));
}

TEST_CASE("issue87 metrics: peak metrics are not computable without tones", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};

    const MetricDefinition *power = metric("peak_power_dBm");
    REQUIRE(power != nullptr);
    REQUIRE(std::isnan(power->compute(spec)));
}

TEST_CASE("issue87 metrics: peak metrics reject non-finite tone fields", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};
    spec.tones = {{1e9, std::numeric_limits<double>::quiet_NaN(), 0.0}, {2e9, -10.0, 0.0}};

    REQUIRE(std::isnan(metric("peak_power_dBm")->compute(spec)));
    REQUIRE(std::isnan(metric("peak_freq_Hz")->compute(spec)));
}

TEST_CASE("issue87 metrics: noise floor rejects malformed frequency grids", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9, 4e9};
    spec.noise_total_W = {4.0e-21, 4.0e-21, 4.0e-21};

    REQUIRE(std::isnan(metric("noise_floor_dBm_per_Hz")->compute(spec)));

    Spectrum repeated;
    repeated.frequencies = {1e9, 2e9, 2e9};
    repeated.noise_total_W = {4.0e-21, 4.0e-21, 4.0e-21};
    REQUIRE(std::isnan(metric("noise_floor_dBm_per_Hz")->compute(repeated)));

    Spectrum nonfinite;
    nonfinite.frequencies = {1e9, std::numeric_limits<double>::quiet_NaN()};
    nonfinite.noise_total_W = {4.0e-21, 4.0e-21};
    REQUIRE(std::isnan(metric("noise_floor_dBm_per_Hz")->compute(nonfinite)));
}

TEST_CASE("issue87 metrics: empty noise is a valid silent floor", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};

    const double value = metric("noise_floor_dBm_per_Hz")->compute(spec);
    REQUIRE(std::isinf(value));
    REQUIRE(value < 0.0);
}

TEST_CASE("issue87 metrics: noise floor rejects accumulation overflow", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};
    spec.noise_total_W = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};

    REQUIRE(std::isnan(metric("noise_floor_dBm_per_Hz")->compute(spec)));
}

TEST_CASE("issue87 metrics: finite large noise stays finite after dBm conversion",
          "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};
    const double density = std::numeric_limits<double>::max() / 4.0;
    spec.noise_total_W = {density, density};

    REQUIRE(std::isfinite(metric("noise_floor_dBm_per_Hz")->compute(spec)));
}

TEST_CASE("issue87 metrics: noise_floor_dBm_per_Hz is the mean density in dBm/Hz",
          "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9, 3e9};
    spec.noise_total_W = {4.0e-21, 4.0e-21, 4.0e-21};

    const MetricDefinition *def = metric("noise_floor_dBm_per_Hz");
    REQUIRE(def != nullptr);
    REQUIRE(def->unit == "dBm/Hz");
    // 10*log10(4e-21 * 1000) = 10*log10(4e-18) = -173.9794 dBm/Hz
    REQUIRE(def->compute(spec) == Approx(-173.9794).margin(1e-3));
}

TEST_CASE("issue87 metrics: an invalid grid is not computable", "[issue87][metrics]") {
    Spectrum spec; // no frequencies, no noise

    const MetricDefinition *def = metric("noise_floor_dBm_per_Hz");
    REQUIRE(def != nullptr);
    REQUIRE(std::isnan(def->compute(spec)));
}

TEST_CASE("issue87 metrics: a zero noise density is a valid -inf floor", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};
    spec.noise_total_W = {0.0, 0.0};

    const MetricDefinition *def = metric("noise_floor_dBm_per_Hz");
    REQUIRE(def != nullptr);
    REQUIRE(std::isinf(def->compute(spec)));
    REQUIRE(def->compute(spec) < 0.0);
}

TEST_CASE("issue87 metrics: a negative noise density is not computable", "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9};
    spec.noise_total_W = {-1e-21, 1e-21};

    const MetricDefinition *def = metric("noise_floor_dBm_per_Hz");
    REQUIRE(def != nullptr);
    REQUIRE(std::isnan(def->compute(spec)));
}

TEST_CASE("issue87 metrics: a density grid of the wrong length is not computable",
          "[issue87][metrics]") {
    Spectrum spec;
    spec.frequencies = {1e9, 2e9, 3e9};
    spec.noise_total_W = {4.0e-21, 4.0e-21}; // two densities, three bins

    const MetricDefinition *def = metric("noise_floor_dBm_per_Hz");
    REQUIRE(def != nullptr);
    REQUIRE(std::isnan(def->compute(spec)));
}

TEST_CASE("issue87 metrics: both metrics reject malformed noise identically",
          "[issue87][metrics]") {
    const MetricDefinition *total = metric("power_dBm");
    const MetricDefinition *floor_dBm = metric("noise_floor_dBm_per_Hz");
    REQUIRE(total != nullptr);
    REQUIRE(floor_dBm != nullptr);

    SECTION("negative density bin") {
        Spectrum spec;
        spec.frequencies = {1e9, 2e9};
        spec.noise_total_W = {-1e-21, 1e-21};
        REQUIRE(std::isnan(total->compute(spec)));
        REQUIRE(std::isnan(floor_dBm->compute(spec)));
    }

    SECTION("density grid shorter than the frequency grid") {
        Spectrum spec;
        spec.frequencies = {1e9, 2e9, 3e9};
        spec.noise_total_W = {4e-21, 4e-21};
        REQUIRE(std::isnan(total->compute(spec)));
        REQUIRE(std::isnan(floor_dBm->compute(spec)));
    }

    // Same input, serialized exactly as the runner produces it: the unmeasurable
    // value must encode as a null with valid=false, so a consumer cannot confuse
    // it with the well-formed silent case (null value, valid=true).
    Spectrum malformed;
    malformed.frequencies = {1e9, 2e9};
    malformed.noise_total_W = {-1e-21, 1e-21};
    const double measured = total->compute(malformed);
    FlowResult result;
    result.ok = true;
    result.name = "malformed";
    FlowRow row;
    row.metrics.push_back(
        MetricSample{"power_dBm", 101, 0, measured, total->unit, !std::isnan(measured)});
    result.rows.push_back(row);

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["valid"] == false);
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["value"].is_null());
}

TEST_CASE("issue87 metrics: the registry exposes the four built-ins", "[issue87][metrics]") {
    const auto names = MetricRegistry::instance().names();
    REQUIRE(names.size() == 4);
    REQUIRE(MetricRegistry::instance().find("power_dBm") != nullptr);
    REQUIRE(MetricRegistry::instance().find("peak_power_dBm") != nullptr);
    REQUIRE(MetricRegistry::instance().find("peak_freq_Hz") != nullptr);
    REQUIRE(MetricRegistry::instance().find("noise_floor_dBm_per_Hz") != nullptr);
    REQUIRE(MetricRegistry::instance().find("no_such_metric") == nullptr);
}

TEST_CASE("issue87 json: a finite metric is a JSON number with valid=true", "[issue87][json]") {
    FlowResult result;
    result.ok = true;
    result.name = "finite";
    FlowRow row;
    row.metrics.push_back(MetricSample{"power_dBm", 101, 0, 0.0, "dBm", true});
    result.rows.push_back(row);

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["ok"] == true);
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["value"].is_number());
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["value"].get<double>() == 0.0);
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["valid"] == true);
}

TEST_CASE("issue87 json: a -inf metric is null with valid=true", "[issue87][json]") {
    FlowResult result;
    result.ok = true;
    result.name = "silent";
    FlowRow row;
    row.metrics.push_back(
        MetricSample{"power_dBm", 101, 0, -std::numeric_limits<double>::infinity(), "dBm", true});
    result.rows.push_back(row);

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["value"].is_null());
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:power_dBm"]["valid"] == true);
}

TEST_CASE("issue87 json: an uncomputable metric is null with valid=false", "[issue87][json]") {
    FlowResult result;
    result.ok = true;
    result.name = "undefined";
    FlowRow row;
    row.metrics.push_back(MetricSample{"peak_power_dBm", 101, 0,
                                       std::numeric_limits<double>::quiet_NaN(), "dBm", false});
    result.rows.push_back(row);

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:peak_power_dBm"]["value"].is_null());
    REQUIRE(parsed["rows"][0]["metrics"]["101:0:peak_power_dBm"]["valid"] == false);
}

TEST_CASE("issue87 json: the same metric at two points is not overwritten", "[issue87][json]") {
    FlowResult result;
    result.ok = true;
    result.name = "two points";
    FlowRow row;
    row.metrics.push_back(MetricSample{"power_dBm", 100, 0, -30.0, "dBm", true});
    row.metrics.push_back(MetricSample{"power_dBm", 102, 0, -40.0, "dBm", true});
    result.rows.push_back(row);

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["rows"][0]["metrics"].size() == 2);
    REQUIRE(parsed["rows"][0]["metrics"]["100:0:power_dBm"]["value"].get<double>() == -30.0);
    REQUIRE(parsed["rows"][0]["metrics"]["102:0:power_dBm"]["value"].get<double>() == -40.0);
}

TEST_CASE("issue87 json: conditions are keyed by component and path", "[issue87][json]") {
    FlowResult result;
    result.ok = true;
    result.name = "keyed";
    FlowRow row;
    row.conditions.push_back(ConditionValue{100, "tones[0].power_dBm", -30.0});
    result.rows.push_back(row);

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["rows"][0]["conditions"]["100:tones[0].power_dBm"].get<double>() == -30.0);
}

TEST_CASE("issue87 json: a failed result reports its error code name", "[issue87][json]") {
    FlowResult result;
    result.ok = false;
    result.error = {FlowErrorCode::UnknownMetric, "measure[0]: unknown metric 'nope'"};
    result.name = "failed";

    const nlohmann::json parsed = nlohmann::json::parse(result.toJson().dump());
    REQUIRE(parsed["ok"] == false);
    REQUIRE(parsed["error"]["code"] == "unknown_metric");
    REQUIRE(parsed["error"]["message"] == "measure[0]: unknown metric 'nope'");
    REQUIRE(parsed["rows"].empty());
}

TEST_CASE("issue87 params: writes a float slot in place", "[issue87][params]") {
    nlohmann::json snapshot = {{"gain_dB", 20.0}};
    std::string error;
    REQUIRE(applyConditionValue(snapshot, "gain_dB", -30.0, &error));
    REQUIRE(snapshot["gain_dB"].is_number_float());
    REQUIRE(snapshot["gain_dB"].get<double>() == Approx(-30.0));
}

TEST_CASE("issue87 params: an integer slot stays an integer", "[issue87][params]") {
    nlohmann::json snapshot = {{"decimation", 2}};
    std::string error;
    REQUIRE(applyConditionValue(snapshot, "decimation", 8.0, &error));
    REQUIRE(snapshot["decimation"].is_number_integer());
    REQUIRE(snapshot["decimation"].get<int>() == 8);
}

TEST_CASE("issue87 params: a fractional value is rejected for an integer slot",
          "[issue87][params]") {
    nlohmann::json snapshot = {{"decimation", 2}};
    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "decimation", 4.5, &error));
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("issue87 params: an unsigned slot stays unsigned", "[issue87][params]") {
    nlohmann::json snapshot = {{"count", 5u}};
    REQUIRE(snapshot["count"].is_number_unsigned());

    std::string error;
    REQUIRE(applyConditionValue(snapshot, "count", 7.0, &error));
    REQUIRE(snapshot["count"].is_number_unsigned());
    REQUIRE(snapshot["count"].get<unsigned>() == 7u);
}

TEST_CASE("issue87 params: a negative value is rejected for an unsigned slot",
          "[issue87][params]") {
    nlohmann::json snapshot = {{"count", 5u}};
    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "count", -1.0, &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(snapshot["count"].get<unsigned>() == 5u);
}

TEST_CASE("issue87 params: addresses array elements", "[issue87][params]") {
    nlohmann::json snapshot;
    snapshot["tones"] = nlohmann::json::array();
    snapshot["tones"].push_back(nlohmann::json{{"freq_Hz", 1e9}, {"power_dBm", -30.0}});

    std::string error;
    REQUIRE(applyConditionValue(snapshot, "tones[0].power_dBm", -10.0, &error));
    REQUIRE(snapshot["tones"][0]["power_dBm"].is_number_float());
    REQUIRE(snapshot["tones"][0]["power_dBm"].get<double>() == Approx(-10.0));
    REQUIRE(snapshot["tones"][0]["freq_Hz"].get<double>() == Approx(1e9));
}

TEST_CASE("issue87 params: rejects a path that does not resolve", "[issue87][params]") {
    nlohmann::json snapshot = {{"gain_dB", 20.0}};
    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "nf_dB", 3.0, &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[0].power_dBm", -10.0, &error));
}

TEST_CASE("issue87 params: rejects an out-of-range index", "[issue87][params]") {
    nlohmann::json snapshot;
    snapshot["tones"] = nlohmann::json::array();
    snapshot["tones"].push_back(nlohmann::json{{"power_dBm", -30.0}});

    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[1].power_dBm", -10.0, &error));
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("issue87 params: rejects a non-numeric slot", "[issue87][params]") {
    nlohmann::json snapshot = {{"filter_type", "LPF"}, {"sparam_mode", false}};
    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "filter_type", 1.0, &error));
    REQUIRE_FALSE(applyConditionValue(snapshot, "sparam_mode", 1.0, &error));
}

TEST_CASE("issue87 params: rejects malformed paths", "[issue87][params]") {
    nlohmann::json snapshot = {{"gain_dB", 20.0}};
    std::string error;
    REQUIRE_FALSE(applyConditionValue(snapshot, "", 1.0, &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(applyConditionValue(snapshot, "gain_dB.", 1.0, &error));
    REQUIRE_FALSE(applyConditionValue(snapshot, "gain_dB]", 1.0, &error));
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[0", 1.0, &error));
    REQUIRE_FALSE(applyConditionValue(snapshot, "tones[x]", 1.0, &error));
}

namespace {
std::string writeTempFlow(const std::string &name, const std::string &json) {
    const std::string path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream out(path);
    out << json;
    return path;
}
const std::string kValidFlow = R"({
  "version": 1,
  "name": "unit",
  "conditions": [
    {"component": 100, "path": "tones[0].power_dBm", "values": [-30, -20]}
  ],
  "measure": [
    {"component": 101, "port": 0, "metric": "power_dBm"}
  ]
})";
} // namespace

TEST_CASE("issue87 loader: a valid flow file loads", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_valid.flow.json", kValidFlow));
    REQUIRE(loaded.ok);
    REQUIRE(loaded.spec.version == 1);
    REQUIRE(loaded.spec.name == "unit");
    REQUIRE(loaded.spec.conditions.size() == 1);
    REQUIRE(loaded.spec.conditions[0].component == 100);
    REQUIRE(loaded.spec.conditions[0].path == "tones[0].power_dBm");
    REQUIRE(loaded.spec.conditions[0].values == std::vector<double>{-30, -20});
    REQUIRE(loaded.spec.measure.size() == 1);
    REQUIRE(loaded.spec.measure[0].component == 101);
    REQUIRE(loaded.spec.measure[0].port == 0);
    REQUIRE(loaded.spec.measure[0].metric == "power_dBm");
}

TEST_CASE("issue87 loader: conditions are optional and default to no sweep", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_nosweep.flow.json",
                      R"({"version": 1, "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE(loaded.ok);
    REQUIRE(loaded.spec.conditions.empty());
    REQUIRE(loaded.spec.measure[0].port == 0);           // defaulted
    REQUIRE(loaded.spec.name == "issue87_nosweep.flow"); // defaults to the file stem
}

TEST_CASE("issue87 loader: a missing file is reported", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(
        (std::filesystem::temp_directory_path() / "issue87_does_not_exist.flow.json").string());
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::FileUnreadable);
}

TEST_CASE("issue87 loader: malformed JSON is reported", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_bad.flow.json", "{ not json"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::InvalidJson);
}

TEST_CASE("issue87 loader: a non-object root is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_root.flow.json", "[1, 2, 3]"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: an unsupported version is rejected", "[issue87][loader]") {
    for (const std::string version : {"0", "2"}) {
        const auto loaded = LoadFlowFile(
            writeTempFlow("issue87_version.flow.json",
                          R"({"version": )" + version +
                              R"(, "measure": [{"component": 1, "metric": "power_dBm"}]})"));
        REQUIRE_FALSE(loaded.ok);
        REQUIRE(loaded.error.code == FlowErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("issue87 loader: a missing version is rejected", "[issue87][loader]") {
    const auto loaded =
        LoadFlowFile(writeTempFlow("issue87_noversion.flow.json",
                                   R"({"measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: a missing or empty measure is rejected", "[issue87][loader]") {
    const auto missing =
        LoadFlowFile(writeTempFlow("issue87_nomeasure.flow.json", R"({"version": 1})"));
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.error.code == FlowErrorCode::WrongShape);

    const auto empty = LoadFlowFile(
        writeTempFlow("issue87_emptymeasure.flow.json", R"({"version": 1, "measure": []})"));
    REQUIRE_FALSE(empty.ok);
    REQUIRE(empty.error.code == FlowErrorCode::EmptyMeasurement);
}

TEST_CASE("issue87 loader: an unknown metric is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_badmetric.flow.json",
                      R"({"version": 1, "measure": [{"component": 1, "metric": "nope"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::UnknownMetric);
}

TEST_CASE("issue87 loader: an empty values array is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow(
        "issue87_emptyvalues.flow.json",
        R"({"version": 1, "conditions": [{"component": 100, "path": "gain_dB", "values": []}],
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: a non-array conditions section is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_badconditions.flow.json",
                                                   R"({"version": 1, "conditions": 5,
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: a non-numeric value is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_badvalue.flow.json",
                                                   R"({"version": 1,
            "conditions": [{"component": 100, "path": "gain_dB", "values": ["x"]}],
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::BadFieldType);
}

TEST_CASE("issue87 loader: a duplicate condition target is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_duplicate.flow.json",
                                                   R"({"version": 1,
            "conditions": [{"component": 100, "path": "gain_dB", "values": [1]},
                           {"component": 100, "path": "gain_dB", "values": [2]}],
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::DuplicateConditionTarget);
}

TEST_CASE("issue87 loader: a duplicate measurement is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_dupmeasure.flow.json",
                                                   R"({"version": 1,
            "measure": [{"component": 101, "port": 0, "metric": "power_dBm"},
                        {"component": 101, "port": 0, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::DuplicateMeasurement);
}

TEST_CASE("issue87 loader: a conditions entry that is not an object is rejected",
          "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_cond_entry.flow.json",
                                                   R"({"version": 1, "conditions": [5],
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: out-of-range integer fields are rejected", "[issue87][loader]") {
    const auto bad_version = LoadFlowFile(writeTempFlow(
        "issue87_version_range.flow.json",
        R"({"version": 4294967297, "measure": [{"component": 1, "metric": "power_dBm"}]})"));

    const auto bad_negative =
        LoadFlowFile(writeTempFlow("issue87_negative_component_range.flow.json",
                                   R"({"version": 1, "measure": [{"component": -2147483649,
            "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(bad_negative.ok);
    REQUIRE(bad_negative.error.code == FlowErrorCode::BadFieldType);
    REQUIRE_FALSE(bad_version.ok);
    REQUIRE(bad_version.error.code == FlowErrorCode::BadFieldType);

    const auto bad_component =
        LoadFlowFile(writeTempFlow("issue87_component_range.flow.json",
                                   R"({"version": 1, "conditions": [{"component": 4294967296,
            "path": "gain_dB", "values": [1]}],
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(bad_component.ok);
    REQUIRE(bad_component.error.code == FlowErrorCode::BadFieldType);

    const auto bad_port = LoadFlowFile(
        writeTempFlow("issue87_port_range.flow.json",
                      R"({"version": 1, "measure": [{"component": 1, "port": 4294967296,
            "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(bad_port.ok);
    REQUIRE(bad_port.error.code == FlowErrorCode::BadFieldType);
}

TEST_CASE("issue87 runner: dangling links do not create a false cycle", "[issue87][runner]") {
    NodeGraphEngine graph;
    AmplifierEngine downstream(101, graph);
    AmplifierEngine upstream(100, graph);
    graph.addLink(999, upstream.inputPinId());
    graph.addLink(upstream.outputPinId(), downstream.inputPinId());
    std::vector<IComponentEngine *> comps{&downstream, &upstream};

    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_dangling_link.flow.json",
                      R"({"version": 1, "measure": [{"component": 101, "metric": "power_dBm"}]})"));
    REQUIRE(loaded.ok);

    const FlowResult result = RunFlow(loaded.spec, comps, graph);
    REQUIRE(result.ok);
    REQUIRE(result.rows.size() == 1);
}

TEST_CASE("issue87 runner: deserialize failure is a typed flow error", "[issue87][runner]") {
    NodeGraphEngine graph;
    ThrowingEngine engine(300, graph);
    std::vector<IComponentEngine *> comps{&engine};

    FlowSpec spec;
    spec.name = "throwing";
    spec.conditions.push_back(Condition{300, "value", {1.0}});
    spec.measure.push_back(Measurement{300, 0, "power_dBm"});
    const nlohmann::json before = engine.serialize();

    FlowResult result;
    REQUIRE_NOTHROW(result = RunFlow(spec, comps, graph));
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.code == FlowErrorCode::DeserializeFailed);
    REQUIRE(result.rows.empty());
    REQUIRE(engine.serialize() == before);
}

TEST_CASE("issue87 runner: rollback failure is reported", "[issue87][runner]") {
    NodeGraphEngine graph;
    ThrowingEngine engine(301, graph, true);
    std::vector<IComponentEngine *> comps{&engine};

    FlowSpec spec;
    spec.name = "rollback-failure";
    spec.conditions.push_back(Condition{301, "value", {1.0}});
    spec.measure.push_back(Measurement{301, 0, "power_dBm"});

    const FlowResult result = RunFlow(spec, comps, graph);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.code == FlowErrorCode::DeserializeFailed);
    REQUIRE(result.error.message.find("rollback failed") != std::string::npos);
    REQUIRE(result.rows.empty());
}

TEST_CASE("issue87 loader: a conditions entry missing a required field is rejected",
          "[issue87][loader]") {
    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_cond_missing.flow.json",
                      R"({"version": 1, "conditions": [{"component": 100, "path": "gain_dB"}],
            "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: a measure entry that is not an object is rejected",
          "[issue87][loader]") {
    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_measure_entry.flow.json", R"({"version": 1, "measure": [5]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: a measure entry missing 'metric' is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow(
        "issue87_measure_missing.flow.json", R"({"version": 1, "measure": [{"component": 1}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::WrongShape);
}

TEST_CASE("issue87 loader: a non-integer version is rejected", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_version_type.flow.json",
                      R"({"version": "1", "measure": [{"component": 1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::BadFieldType);
}

TEST_CASE("issue87 loader: a negative measure port is a bad field type", "[issue87][loader]") {
    const auto loaded = LoadFlowFile(writeTempFlow(
        "issue87_negative_port.flow.json",
        R"({"version": 1, "measure": [{"component": 1, "port": -1, "metric": "power_dBm"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::BadFieldType);
}

TEST_CASE("issue87 loader: a failed load returns an empty spec", "[issue87][loader]") {
    // A valid conditions section followed by an invalid measure section must not
    // leave the returned spec half-populated.
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_partial_spec.flow.json",
                                                   R"({"version": 1,
            "conditions": [{"component": 100, "path": "gain_dB", "values": [1]}],
            "measure": [{"component": 1, "metric": "nope"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::UnknownMetric);
    REQUIRE(loaded.spec.conditions.empty());
    REQUIRE(loaded.spec.measure.empty());
    REQUIRE(loaded.spec.name.empty());
}

TEST_CASE("issue87 loader: a failure after a valid measurement still returns an empty spec",
          "[issue87][loader]") {
    // measure[0] is valid and gets pushed; measure[1] fails. The returned spec
    // must therefore discard an already-populated measurement, not merely one
    // that was never added.
    const auto loaded = LoadFlowFile(writeTempFlow("issue87_partial_measure.flow.json",
                                                   R"({"version": 1,
            "measure": [{"component": 1, "metric": "power_dBm"},
                        {"component": 2, "metric": "nope"}]})"));
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.error.code == FlowErrorCode::UnknownMetric);
    REQUIRE(loaded.spec.measure.empty());
}

namespace {

constexpr int kGenId = 100;
constexpr int kAmpId = 101;
constexpr int kAttenId = 102;
constexpr int kAdcId = 200;
constexpr int kPfbId = 201;

std::string fixture(const std::string &name) {
    return std::string(PROJECT_SOURCE_DIR) + "/tests/flows/" + name;
}

// Returns the reading for (component, port, metric) in row `row`; NaN when absent.
double metricValue(const FlowResult &result, size_t row, int component, int port,
                   const std::string &metric) {
    REQUIRE(row < result.rows.size());
    for (const auto &sample : result.rows[row].metrics) {
        if (sample.component == component && sample.port == port && sample.name == metric)
            return sample.value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

TEST_CASE("issue87 runner: a power sweep tracks drive plus gain", "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    gen.addTone(1e9, -30.0, 0.0);
    amp.deserialize(nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 3.0}});
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &amp};

    const auto loaded = LoadFlowFile(fixture("amp_power_sweep.flow.json"));
    REQUIRE(loaded.ok);
    const FlowResult result = RunFlow(loaded.spec, comps, graph);

    REQUIRE(result.ok);
    REQUIRE(result.rows.size() == 3);
    // The generator's own thermal floor (k*T over the full grid) adds ~1.6e-10 W
    // against a 1e-6 W tone, so the total lands within 0.001 dB of drive + 20.
    REQUIRE(metricValue(result, 0, kAmpId, 0, "power_dBm") == Approx(-10.0).margin(0.01));
    REQUIRE(metricValue(result, 1, kAmpId, 0, "power_dBm") == Approx(0.0).margin(0.01));
    REQUIRE(metricValue(result, 2, kAmpId, 0, "power_dBm") == Approx(10.0).margin(0.01));
    REQUIRE(result.rows[0].conditions[0].path == "tones[0].power_dBm");
    REQUIRE(result.rows[0].conditions[0].value == Approx(-30.0));
    REQUIRE(result.rows[0].metrics[0].valid);
}

TEST_CASE("issue87 runner: two conditions sweep the cartesian product in order",
          "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    gen.addTone(1e9, -30.0, 0.0);
    amp.deserialize(nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 3.0}});
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &amp};

    const auto loaded = LoadFlowFile(fixture("amp_power_freq_sweep.flow.json"));
    REQUIRE(loaded.ok);
    const FlowResult result = RunFlow(loaded.spec, comps, graph);

    REQUIRE(result.ok);
    REQUIRE(result.rows.size() == 4);
    // First condition (power) is outermost, so it varies slowest.
    REQUIRE(result.rows[0].conditions[0].value == Approx(-30.0));
    REQUIRE(result.rows[0].conditions[1].value == Approx(1e9));
    REQUIRE(result.rows[1].conditions[0].value == Approx(-30.0));
    REQUIRE(result.rows[1].conditions[1].value == Approx(2e9));
    REQUIRE(result.rows[2].conditions[0].value == Approx(-20.0));
    REQUIRE(result.rows[2].conditions[1].value == Approx(1e9));
    REQUIRE(result.rows[3].conditions[0].value == Approx(-20.0));
    REQUIRE(result.rows[3].conditions[1].value == Approx(2e9));

    REQUIRE(metricValue(result, 0, kAmpId, 0, "peak_freq_Hz") == Approx(1e9).margin(1.0));
    REQUIRE(metricValue(result, 1, kAmpId, 0, "peak_freq_Hz") == Approx(2e9).margin(1.0));
    REQUIRE(metricValue(result, 0, kAmpId, 0, "peak_power_dBm") == Approx(-10.0).margin(0.01));
    REQUIRE(metricValue(result, 3, kAmpId, 0, "peak_power_dBm") == Approx(0.0).margin(0.01));
}

TEST_CASE("issue87 runner: an attenuator drops the measured power by its attenuation",
          "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AttenuatorEngine atten(kAttenId, graph);
    gen.addTone(1e9, -30.0, 0.0);
    // Canonical key from serialize() is atten_dB (the inspector field key is
    // attenuation_dB — the harness always uses the serialize key).
    atten.deserialize(nlohmann::json{{"atten_dB", 10.0}, {"sparam_mode", false}});
    graph.addLink(gen.outputPinId(), atten.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &atten};

    const auto loaded = LoadFlowFile(fixture("gen_atten_power.flow.json"));
    REQUIRE(loaded.ok);
    const FlowResult result = RunFlow(loaded.spec, comps, graph);

    REQUIRE(result.ok);
    REQUIRE(result.rows.size() == 1);
    // The generator floor passes the attenuator nearly unchanged (the matched
    // attenuator attenuates it by 10 dB and adds 10 dB of noise), so the ratio
    // is 9.994 dB, not exactly 10.
    const double gen_power = metricValue(result, 0, kGenId, 0, "power_dBm");
    const double atten_power = metricValue(result, 0, kAttenId, 0, "power_dBm");
    REQUIRE(gen_power - atten_power == Approx(9.994).margin(0.05));
}

TEST_CASE("issue87 runner: the amplifier output floor rises by gain plus noise figure",
          "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    gen.addTone(1e9, -30.0, 0.0);
    amp.deserialize(nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 3.0}});
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &amp};

    const auto loaded = LoadFlowFile(fixture("noise_floor.flow.json"));
    REQUIRE(loaded.ok);
    const FlowResult result = RunFlow(loaded.spec, comps, graph);

    REQUIRE(result.ok);
    REQUIRE(result.rows.size() == 1);

    // Derivation from common.h's addedNoiseDensity_W_per_Hz(nf_dB, G):
    //   generator floor          = k*T                       = 4.0037e-21 W/Hz
    //   amp passes input noise   = G * k*T                   (G = 100)
    //   amp adds                 = k * T*(F-1) * G           (F = 1.99526)
    //   output / input           = G + (F-1)*G = G*F = 199.53 -> 23.0 dB
    const double gen_floor = metricValue(result, 0, kGenId, 0, "noise_floor_dBm_per_Hz");
    const double amp_floor = metricValue(result, 0, kAmpId, 0, "noise_floor_dBm_per_Hz");
    REQUIRE(std::isfinite(gen_floor));
    REQUIRE(std::isfinite(amp_floor));
    REQUIRE(amp_floor - gen_floor == Approx(23.0).margin(0.5));
}

TEST_CASE("issue87 runner: a repeated run reproduces identical results", "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    gen.addTone(1e9, -30.0, 0.0);
    amp.deserialize(nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 3.0}});
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &amp};

    const auto loaded = LoadFlowFile(fixture("amp_power_sweep.flow.json"));
    REQUIRE(loaded.ok);

    const FlowResult first = RunFlow(loaded.spec, comps, graph);
    const FlowResult second = RunFlow(loaded.spec, comps, graph);
    REQUIRE(first.ok);
    REQUIRE(second.ok);
    REQUIRE(first.toJson().dump() == second.toJson().dump());
}

TEST_CASE("issue87 runner: reversing condition order yields identical values per configuration",
          "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    gen.addTone(1e9, -30.0, 0.0);
    amp.deserialize(nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 3.0}});
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &amp};

    const auto loaded = LoadFlowFile(fixture("amp_power_freq_sweep.flow.json"));
    REQUIRE(loaded.ok);

    FlowSpec reversed = loaded.spec;
    std::reverse(reversed.conditions.begin(), reversed.conditions.end());
    REQUIRE(reversed.conditions.size() == loaded.spec.conditions.size());

    const FlowResult forward_result = RunFlow(loaded.spec, comps, graph);
    const FlowResult reverse_result = RunFlow(reversed, comps, graph);
    REQUIRE(forward_result.ok);
    REQUIRE(reverse_result.ok);
    REQUIRE(forward_result.rows.size() == reverse_result.rows.size());

    // Key each row by its full condition set, so the two runs' different emission
    // orders do not matter: reversing the sweep order must not change any
    // configuration's measurement. A runner that let the previous row's state
    // leak through (e.g. only re-patched the condition the odometer advanced and
    // left the rest stale) would disagree here.
    const auto configuration = [](const FlowRow &row) {
        std::map<std::string, double> values;
        for (const auto &condition : row.conditions)
            values[std::to_string(condition.component) + ":" + condition.path] = condition.value;
        return values;
    };
    const auto metric_of = [](const FlowRow &row, const std::string &name) {
        for (const auto &sample : row.metrics)
            if (sample.name == name)
                return sample.value;
        return std::numeric_limits<double>::quiet_NaN();
    };

    for (const FlowRow &forward_row : forward_result.rows) {
        const auto wanted = configuration(forward_row);
        bool matched = false;
        for (const FlowRow &reverse_row : reverse_result.rows) {
            if (!(configuration(reverse_row) == wanted))
                continue;
            matched = true;
            REQUIRE(metric_of(reverse_row, "peak_power_dBm") ==
                    Approx(metric_of(forward_row, "peak_power_dBm")).margin(1e-9));
            REQUIRE(metric_of(reverse_row, "peak_freq_Hz") ==
                    Approx(metric_of(forward_row, "peak_freq_Hz")).margin(1e-6));
        }
        REQUIRE(matched);
    }
}

TEST_CASE("issue87 runner: an unknown component is reported before any run", "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    graph.addLink(gen.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&gen, &amp};

    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_missing_component.flow.json",
                      R"({"version": 1, "measure": [{"component": 999, "metric": "power_dBm"}]})"));
    REQUIRE(loaded.ok);

    const FlowResult result = RunFlow(loaded.spec, comps, graph);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.code == FlowErrorCode::ComponentNotFound);
    REQUIRE(result.rows.empty());
}

TEST_CASE("issue87 runner: an out-of-range port is reported before any run", "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    std::vector<IComponentEngine *> comps{&gen};

    const auto loaded = LoadFlowFile(writeTempFlow(
        "issue87_bad_port.flow.json",
        R"({"version": 1, "measure": [{"component": 100, "port": 3, "metric": "power_dBm"}]})"));
    REQUIRE(loaded.ok);

    const FlowResult result = RunFlow(loaded.spec, comps, graph);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.code == FlowErrorCode::PortOutOfRange);
    REQUIRE(result.rows.empty());
}

TEST_CASE("issue87 runner: an inapplicable condition path is reported before any run",
          "[issue87][runner]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    std::vector<IComponentEngine *> comps{&gen};

    const auto loaded = LoadFlowFile(writeTempFlow("issue87_bad_path.flow.json",
                                                   R"({"version": 1,
            "conditions": [{"component": 100, "path": "no_such_key", "values": [1]}],
            "measure": [{"component": 100, "metric": "power_dBm"}]})"));
    REQUIRE(loaded.ok);

    const FlowResult result = RunFlow(loaded.spec, comps, graph);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.code == FlowErrorCode::PathNotApplicable);
    REQUIRE(result.rows.empty());
}

TEST_CASE("issue87 runner: the shared link policy rejects a non-ADC source into a PFB",
          "[issue87][runner]") {
    // Illegal: generator -> PFB. The graph accepts the link; the runner's rewire
    // pass must leave the PFB input null, exactly as the app's canvas would.
    {
        NodeGraphEngine graph;
        SignalGeneratorEngine gen(kGenId, graph);
        PFBChannelizerEngine pfb(kPfbId, graph);
        gen.addTone(1e9, -30.0, 0.0);
        graph.addLink(gen.outputPinId(), pfb.inputPinId());
        std::vector<IComponentEngine *> comps{&gen, &pfb};

        const auto loaded = LoadFlowFile(writeTempFlow(
            "issue87_illegal_link.flow.json",
            R"({"version": 1, "measure": [{"component": 201, "port": 0, "metric": "power_dBm"}]})"));
        REQUIRE(loaded.ok);
        const FlowResult result = RunFlow(loaded.spec, comps, graph);
        REQUIRE(result.ok);
        REQUIRE(pfb.node().inputs[0] == nullptr);
    }

    // Legal: ADC -> PFB.
    {
        NodeGraphEngine graph;
        AdcEngine adc(kAdcId, graph);
        PFBChannelizerEngine pfb(kPfbId, graph);
        graph.addLink(adc.outputPinId(), pfb.inputPinId());
        std::vector<IComponentEngine *> comps{&adc, &pfb};

        const auto loaded = LoadFlowFile(writeTempFlow(
            "issue87_legal_link.flow.json",
            R"({"version": 1, "measure": [{"component": 201, "port": 0, "metric": "power_dBm"}]})"));
        REQUIRE(loaded.ok);
        const FlowResult result = RunFlow(loaded.spec, comps, graph);
        REQUIRE(result.ok);
        REQUIRE(pfb.node().inputs[0] != nullptr);
    }
}

TEST_CASE("issue87 runner: a cyclic circuit is rejected", "[issue87][runner]") {
    NodeGraphEngine graph;
    AmplifierEngine amp(kAmpId, graph);
    AmplifierEngine amp2(kAmpId + 1, graph);
    graph.addLink(amp.outputPinId(), amp2.inputPinId());
    graph.addLink(amp2.outputPinId(), amp.inputPinId());
    std::vector<IComponentEngine *> comps{&amp, &amp2};

    const auto loaded = LoadFlowFile(
        writeTempFlow("issue87_cycle.flow.json",
                      R"({"version": 1, "measure": [{"component": 101, "metric": "power_dBm"}]})"));
    REQUIRE(loaded.ok);

    // topologicalOrder() returns both nodes (it appends the ones it could not
    // order), so only the link-order check can catch this.
    const FlowResult result = RunFlow(loaded.spec, comps, graph);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.code == FlowErrorCode::CyclicGraph);
    REQUIRE(result.rows.empty());
}

TEST_CASE("issue87 runner: every sweep value is validated before the first row",
          "[issue87][runner]") {
    NodeGraphEngine graph;
    AdcEngine adc(kAdcId, graph);
    std::vector<IComponentEngine *> comps{&adc};

    SECTION("a later invalid value rejects the whole flow without touching the engine") {
        // 8 is a valid integral decimation, 4.5 is not: the whole flow must be
        // rejected before any row is computed, not half-way through the sweep.
        const auto loaded = LoadFlowFile(writeTempFlow("issue87_late_bad_value.flow.json",
                                                       R"({"version": 1,
            "conditions": [{"component": 200, "path": "decimation", "values": [8, 4.5]}],
            "measure": [{"component": 200, "metric": "power_dBm"}]})"));
        REQUIRE(loaded.ok);

        // Pre-run validation only ever patches COPIES of each serialize() snapshot
        // and never calls deserialize(), so a rejected flow must leave the engine
        // byte-identical. A runner that validated only values[0] and failed mid-sweep
        // would have deserialized 8.0 into the ADC before reaching 4.5.
        const nlohmann::json before = adc.serialize();

        const FlowResult result = RunFlow(loaded.spec, comps, graph);
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.code == FlowErrorCode::PathNotApplicable);
        REQUIRE(result.rows.empty());
        REQUIRE(adc.serialize() == before);
    }

    SECTION("the same flow with only the valid value runs one row") {
        // Positive control: the rejection above must be caused by 4.5, not by 8.
        const auto loaded = LoadFlowFile(writeTempFlow("issue87_single_good_value.flow.json",
                                                       R"({"version": 1,
            "conditions": [{"component": 200, "path": "decimation", "values": [8]}],
            "measure": [{"component": 200, "metric": "power_dBm"}]})"));
        REQUIRE(loaded.ok);

        const FlowResult result = RunFlow(loaded.spec, comps, graph);
        REQUIRE(result.ok);
        REQUIRE(result.rows.size() == 1);
        REQUIRE(result.rows[0].conditions[0].value == Approx(8.0));
    }
}

TEST_CASE("issue87 stability: serialize -> deserialize -> serialize is identical for swept engines",
          "[issue87][stability]") {
    NodeGraphEngine graph;
    SignalGeneratorEngine gen(kGenId, graph);
    AmplifierEngine amp(kAmpId, graph);
    AttenuatorEngine atten(kAttenId, graph);
    AdcEngine adc(kAdcId, graph);

    gen.addTone(1e9, -30.0, 45.0);
    amp.deserialize(nlohmann::json{{"gain_dB", 20.0}, {"nf_dB", 3.0}});
    atten.deserialize(nlohmann::json{{"atten_dB", 10.0}, {"sparam_mode", false}});
    adc.deserialize(nlohmann::json{{"sample_rate_Hz", 1e9},
                                   {"nsd_dBm_per_Hz", -155.0},
                                   {"decimation", 2},
                                   {"nco_fs_fraction", 0.25}});

    const std::vector<IComponentEngine *> engines{&gen, &amp, &atten, &adc};
    for (IComponentEngine *engine : engines) {
        const nlohmann::json first = engine->serialize();
        engine->deserialize(first);
        REQUIRE(engine->serialize() == first);
    }
}
