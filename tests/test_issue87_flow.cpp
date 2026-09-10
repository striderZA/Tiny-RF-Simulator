#include "flow_metrics.h"
#include "flow_result.h"
#include "spectrum.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

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
