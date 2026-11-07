#include "budget_calculator.h"

#include <cmath>
#include <limits>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double dBToLinear(double dB) { return std::pow(10.0, dB / 10.0); }

double linearToDB(double linear) { return 10.0 * std::log10(linear); }

// Appends `text` to an existing diagnostic, one entry per line.
void appendDiagnostic(std::string &diagnostic, const std::string &text) {
    if (!diagnostic.empty())
        diagnostic += "\n";
    diagnostic += text;
}

bool readOptionalString(const nlohmann::json &object, const char *key, std::string &out,
                        std::string &reason) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return true;
    if (!it->is_string()) {
        reason = std::string("`") + key + "` must be a string";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

// Parses one stage entry. Returns false and fills `reason` when the stage is malformed.
bool readStage(const nlohmann::json &value, BudgetStage &stage, std::string &reason) {
    if (!value.is_object()) {
        reason = "expected an object";
        return false;
    }
    const auto type = value.find("type");
    if (type == value.end() || !type->is_string()) {
        reason = "`type` must be a string";
        return false;
    }
    const auto part_number = value.find("part_number");
    if (part_number == value.end() || !part_number->is_string()) {
        reason = "`part_number` must be a string";
        return false;
    }
    stage.type = type->get<std::string>();
    stage.part_number = part_number->get<std::string>();
    if (!readOptionalString(value, "manufacturer", stage.manufacturer, reason) ||
        !readOptionalString(value, "description", stage.description, reason) ||
        !readOptionalString(value, "source_path", stage.source_path, reason))
        return false;

    const auto parameters = value.find("parameters");
    if (parameters != value.end() && !parameters->is_null()) {
        if (!parameters->is_object()) {
            reason = "`parameters` must be an object";
            return false;
        }
        stage.parameters = *parameters;
    }

    const auto data_files = value.find("data_files");
    if (data_files != value.end() && !data_files->is_null()) {
        if (!data_files->is_array()) {
            reason = "`data_files` must be an array";
            return false;
        }
        for (const auto &entry : *data_files) {
            if (!entry.is_object()) {
                reason = "every `data_files` entry must be an object";
                return false;
            }
            const auto file_type = entry.find("type");
            const auto path = entry.find("path");
            if (file_type == entry.end() || !file_type->is_string() || path == entry.end() ||
                !path->is_string()) {
                reason = "every `data_files` entry needs string `type` and `path`";
                return false;
            }
            stage.data_files.push_back(
                DataFileRef{file_type->get<std::string>(), path->get<std::string>()});
        }
    }
    return true;
}

} // namespace

BudgetResult computeBudget(const BudgetConfig &config, const std::vector<double> &frequencies_Hz,
                           const std::vector<BudgetStageMetrics> &stage_metrics) {
    const std::size_t points = frequencies_Hz.size();
    BudgetResult result;
    result.gain_dB.assign(points, kNaN);
    result.nf_dB.assign(points, kNaN);
    result.output_power_dBm.assign(points, kNaN);
    result.iip3_dBm.assign(points, kNaN);
    result.p1db_dBm.assign(points, kNaN);
    result.valid.assign(points, false);

    bool metrics_usable = stage_metrics.size() == config.stages.size();
    for (const auto &metrics : stage_metrics) {
        if (metrics.gain_dB.size() != points || metrics.nf_dB.size() != points ||
            metrics.oip3_dBm.size() != points || metrics.p1db_dBm.size() != points ||
            metrics.out_of_band.size() != points)
            metrics_usable = false;
    }
    if (!metrics_usable)
        return result;

    const double infinity = std::numeric_limits<double>::infinity();
    for (std::size_t point = 0; point < points; ++point) {
        double gain_sum = 0.0;
        double noise_factor_total = 0.0;
        double iip3_inverse = 0.0; // 1/IIP3 in linear mW
        double p1db_inverse = 0.0; // 1/input-referred P1dB in linear mW
        double gain_product = 1.0; // product of the stage gains preceding the current stage
        bool point_valid = true;

        for (std::size_t index = 0; index < stage_metrics.size(); ++index) {
            const BudgetStageMetrics &metrics = stage_metrics[index];
            const double stage_gain_dB = metrics.gain_dB[point];
            const double stage_nf_dB = metrics.nf_dB[point];
            if (metrics.out_of_band[point] || !std::isfinite(stage_gain_dB) ||
                !std::isfinite(stage_nf_dB)) {
                point_valid = false;
                break;
            }
            const double stage_gain = dBToLinear(stage_gain_dB);
            if (!(stage_gain > 0.0) || !std::isfinite(stage_gain)) {
                point_valid = false;
                break;
            }
            const double stage_noise_factor = dBToLinear(stage_nf_dB);

            gain_sum += stage_gain_dB;
            // Friis: F_total = F_1 + (F_2 - 1)/g_1 + (F_3 - 1)/(g_1 g_2) + ...
            noise_factor_total +=
                index == 0 ? stage_noise_factor : (stage_noise_factor - 1.0) / gain_product;

            // Reciprocal-power combination against the gain ahead of this stage. An
            // infinite intercept means no nonlinearity and contributes nothing.
            const double oip3_dBm = metrics.oip3_dBm[point];
            if (std::isfinite(oip3_dBm)) {
                const double input_intercept_mw = dBToLinear(oip3_dBm) / stage_gain;
                if (input_intercept_mw > 0.0)
                    iip3_inverse += gain_product / input_intercept_mw;
            }
            const double p1db_dBm = metrics.p1db_dBm[point];
            if (std::isfinite(p1db_dBm)) {
                const double input_p1db_mw = dBToLinear(p1db_dBm) / stage_gain;
                if (input_p1db_mw > 0.0)
                    p1db_inverse += gain_product / input_p1db_mw;
            }

            gain_product *= stage_gain;
        }

        if (!point_valid)
            continue;

        result.valid[point] = true;
        result.gain_dB[point] = gain_sum;
        result.nf_dB[point] = linearToDB(noise_factor_total);
        result.output_power_dBm[point] = config.input_power_dBm + gain_sum;
        result.iip3_dBm[point] = iip3_inverse > 0.0 ? linearToDB(1.0 / iip3_inverse) : infinity;
        if (p1db_inverse > 0.0)
            result.p1db_dBm[point] = linearToDB(1.0 / p1db_inverse) + gain_sum;
        else
            result.p1db_dBm[point] = infinity;
    }
    return result;
}

nlohmann::json budgetToJson(const BudgetConfig &config) {
    nlohmann::json stages = nlohmann::json::array();
    for (const BudgetStage &stage : config.stages) {
        nlohmann::json data_files = nlohmann::json::array();
        for (const DataFileRef &file : stage.data_files)
            data_files.push_back({{"type", file.type}, {"path", file.path}});

        stages.push_back({{"type", stage.type},
                          {"part_number", stage.part_number},
                          {"manufacturer", stage.manufacturer},
                          {"description", stage.description},
                          {"parameters", stage.parameters.is_null() ? nlohmann::json::object()
                                                                    : stage.parameters},
                          {"data_files", std::move(data_files)},
                          {"source_path", stage.source_path}});
    }
    return nlohmann::json{
        {"version", 1}, {"input_power_dbm", config.input_power_dBm}, {"stages", std::move(stages)}};
}

BudgetConfig budgetFromJson(const nlohmann::json &json, std::string &diagnostic) {
    BudgetConfig config;
    diagnostic.clear();
    if (!json.is_object()) {
        diagnostic = "Budget: expected a JSON object";
        return config;
    }

    const auto version = json.find("version");
    if (version != json.end() && !version->is_null()) {
        // Numeric comparison keeps an out-of-range integer token from wrapping during
        // conversion; any value other than 1 is an unsupported version.
        if (!version->is_number_integer() || *version != 1) {
            diagnostic = "Budget: unsupported version";
            return config;
        }
    }

    const auto input_power = json.find("input_power_dbm");
    if (input_power != json.end() && !input_power->is_null()) {
        if (input_power->is_number())
            config.input_power_dBm = input_power->get<double>();
        else
            appendDiagnostic(diagnostic, "Budget: `input_power_dbm` must be a number");
    }

    const auto stages = json.find("stages");
    if (stages == json.end() || stages->is_null())
        return config;
    if (!stages->is_array()) {
        appendDiagnostic(diagnostic, "Budget: `stages` must be an array");
        return config;
    }

    for (std::size_t index = 0; index < stages->size(); ++index) {
        BudgetStage stage;
        std::string reason;
        if (readStage((*stages)[index], stage, reason)) {
            config.stages.push_back(std::move(stage));
        } else {
            appendDiagnostic(diagnostic, "Stage " + std::to_string(index) + ": " + reason);
        }
    }
    return config;
}
