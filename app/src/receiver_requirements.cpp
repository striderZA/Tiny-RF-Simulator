#include "receiver_requirements.h"

#include "common.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

namespace {
std::string trimAsciiWhitespace(std::string_view text) {
    constexpr std::string_view whitespace = " \t\n\r\f\v";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    const auto last = text.find_last_not_of(whitespace);
    return std::string(text.substr(first, last - first + 1));
}

std::string asciiLowercase(std::string_view text) {
    std::string lowered(text);
    for (char &character : lowered)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return lowered;
}

bool modeNamesEqual(std::string_view first, std::string_view second) {
    return asciiLowercase(first) == asciiLowercase(second);
}

std::optional<double> parseNumber(const std::string &text) {
    if (text.empty())
        return std::nullopt;
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value))
        return std::nullopt;
    return value;
}

ReceiverMetricEvaluation unavailable(ReceiverRequirementStatus status) { return {status, {}, {}}; }

ReceiverMetricEvaluation assess(const std::vector<double> &frequencies,
                                const std::vector<double> &values, double low_Hz, double high_Hz,
                                std::optional<double> minimum, std::optional<double> maximum,
                                bool band_covered) {
    bool any = false;
    bool missing = frequencies.size() != values.size();
    bool failed = false;
    double observed_min = 0.0;
    double observed_max = 0.0;
    const std::size_t aligned_size = std::min(frequencies.size(), values.size());
    for (std::size_t i = 0; i < aligned_size; ++i) {
        const double frequency = frequencies[i];
        if (!std::isfinite(frequency) || frequency < low_Hz || frequency > high_Hz)
            continue;
        if (!std::isfinite(values[i])) {
            missing = true;
            continue;
        }
        const double value = values[i];
        if (!any)
            observed_min = observed_max = value;
        else {
            observed_min = std::min(observed_min, value);
            observed_max = std::max(observed_max, value);
        }
        any = true;
        if ((minimum && value < *minimum) || (maximum && value > *maximum))
            failed = true;
    }
    const auto status =
        failed ? ReceiverRequirementStatus::Fail
               : (!band_covered || !any || missing ? ReceiverRequirementStatus::Incomplete
                                                   : ReceiverRequirementStatus::Pass);
    return {status, any ? std::optional<double>(observed_min) : std::nullopt,
            any ? std::optional<double>(observed_max) : std::nullopt};
}

ReceiverRequirementStatus aggregate(const ReceiverRequirementsEvaluation &result) {
    const ReceiverMetricEvaluation *metrics[] = {&result.gain, &result.noise_figure,
                                                 &result.output_power, &result.iip3};
    bool any_enabled = false;
    bool all_pass = true;
    for (const auto *metric : metrics) {
        if (metric->status == ReceiverRequirementStatus::NotConfigured)
            continue;
        any_enabled = true;
        if (metric->status == ReceiverRequirementStatus::Fail)
            return ReceiverRequirementStatus::Fail;
        all_pass = all_pass && metric->status == ReceiverRequirementStatus::Pass;
    }
    if (!any_enabled)
        return ReceiverRequirementStatus::NotConfigured;
    return all_pass ? ReceiverRequirementStatus::Pass : ReceiverRequirementStatus::Incomplete;
}

nlohmann::json configToJson(const ReceiverRequirementsConfig &config) {
    nlohmann::json result = {{"band_start_hz", config.band_start_Hz},
                             {"band_stop_hz", config.band_stop_Hz}};
    if (config.gain) {
        result["gain_min_db"] = config.gain->minimum_dB;
        result["gain_max_db"] = config.gain->maximum_dB;
    }
    if (config.nf_max_dB)
        result["nf_max_db"] = *config.nf_max_dB;
    if (config.output_power) {
        result["output_power_min_dbm"] = config.output_power->minimum_dBm;
        result["output_power_max_dbm"] = config.output_power->maximum_dBm;
    }
    if (config.iip3_min_dBm)
        result["iip3_min_dbm"] = *config.iip3_min_dBm;
    nlohmann::json conditions = nlohmann::json::object();
    if (config.measurement_conditions.input_level_dBm)
        conditions["input_level_dbm"] = *config.measurement_conditions.input_level_dBm;
    if (config.measurement_conditions.iip3) {
        const auto &settings = *config.measurement_conditions.iip3;
        conditions["iip3"] = {{"tone_spacing_hz", settings.tone_spacing_Hz},
                              {"input_start_dbm", settings.input_start_dBm},
                              {"input_stop_dbm", settings.input_stop_dBm},
                              {"input_step_db", settings.input_step_dB}};
    }
    if (!conditions.empty())
        result["measurement_conditions"] = std::move(conditions);
    return result;
}

std::optional<ReceiverRequirementsConfig> parseConfigObject(const nlohmann::json &value,
                                                            std::string &error) {
    error.clear();
    if (!value.is_object()) {
        error = "Receiver requirements must be a JSON object.";
        return std::nullopt;
    }
    if (!value.contains("band_start_hz") || !value["band_start_hz"].is_number() ||
        !value.contains("band_stop_hz") || !value["band_stop_hz"].is_number()) {
        error = "Receiver requirements band endpoints must be numbers.";
        return std::nullopt;
    }

    ReceiverRequirementsConfig config;
    config.band_start_Hz = value["band_start_hz"].get<double>();
    config.band_stop_Hz = value["band_stop_hz"].get<double>();
    const auto readOptionalNumber = [&value](const char *key, std::optional<double> &field) {
        if (!value.contains(key))
            return true;
        if (!value[key].is_number())
            return false;
        field = value[key].get<double>();
        return true;
    };

    std::optional<double> gain_minimum;
    std::optional<double> gain_maximum;
    if (!readOptionalNumber("gain_min_db", gain_minimum) ||
        !readOptionalNumber("gain_max_db", gain_maximum)) {
        error = "Receiver gain limits must be numbers.";
        return std::nullopt;
    }
    if (gain_minimum.has_value() != gain_maximum.has_value()) {
        error = "Receiver gain minimum and maximum must be saved together.";
        return std::nullopt;
    }
    if (gain_minimum)
        config.gain = ReceiverGainLimits{*gain_minimum, *gain_maximum};
    if (!readOptionalNumber("nf_max_db", config.nf_max_dB)) {
        error = "Receiver noise figure limit must be a number.";
        return std::nullopt;
    }

    std::optional<double> output_minimum;
    std::optional<double> output_maximum;
    if (!readOptionalNumber("output_power_min_dbm", output_minimum) ||
        !readOptionalNumber("output_power_max_dbm", output_maximum)) {
        error = "Receiver output power limits must be numbers.";
        return std::nullopt;
    }
    if (output_minimum.has_value() != output_maximum.has_value()) {
        error = "Receiver output power minimum and maximum must be saved together.";
        return std::nullopt;
    }
    if (output_minimum)
        config.output_power = ReceiverOutputPowerLimits{*output_minimum, *output_maximum};
    if (!readOptionalNumber("iip3_min_dbm", config.iip3_min_dBm)) {
        error = "Receiver IIP3 limit must be a number.";
        return std::nullopt;
    }

    if (value.contains("measurement_conditions")) {
        const auto &conditions = value["measurement_conditions"];
        if (!conditions.is_object()) {
            error = "Receiver measurement conditions must be an object.";
            return std::nullopt;
        }
        if (conditions.contains("input_level_dbm")) {
            if (!conditions["input_level_dbm"].is_number()) {
                error = "Input level must be a number.";
                return std::nullopt;
            }
            config.measurement_conditions.input_level_dBm =
                conditions["input_level_dbm"].get<double>();
        }
        if (conditions.contains("iip3")) {
            const auto &iip3 = conditions["iip3"];
            constexpr std::array<const char *, 4> iip3_keys = {"tone_spacing_hz", "input_start_dbm",
                                                               "input_stop_dbm", "input_step_db"};
            double iip3_fields[4]{};
            if (!iip3.is_object()) {
                error = "IIP3 test settings must be an object.";
                return std::nullopt;
            }
            for (std::size_t i = 0; i < iip3_keys.size(); ++i) {
                if (!iip3.contains(iip3_keys[i]) || !iip3[iip3_keys[i]].is_number()) {
                    error = std::string("IIP3 test field '") + iip3_keys[i] + "' must be a number.";
                    return std::nullopt;
                }
                iip3_fields[i] = iip3[iip3_keys[i]].get<double>();
            }
            config.measurement_conditions.iip3 = ReceiverIIP3TestSettings{
                iip3_fields[0], iip3_fields[1], iip3_fields[2], iip3_fields[3]};
        }
    }
    if (auto reason = validateReceiverRequirementsConfig(config)) {
        error = *reason;
        return std::nullopt;
    }
    return config;
}

ReceiverRequirementsSet invalidRequirementsSet(std::string reason) {
    ReceiverRequirementsSet set;
    set.invalid_reason = std::move(reason);
    return set;
}

// A mode id from a file or an edit: a positive integer that fits the 32-bit id.
std::optional<std::uint32_t> positiveModeId(const nlohmann::json &value) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<unsigned long long>();
        if (number > 0 && number <= std::numeric_limits<std::uint32_t>::max())
            return static_cast<std::uint32_t>(number);
        return std::nullopt;
    }
    if (value.is_number_integer()) {
        const auto number = value.get<long long>();
        if (number > 0 &&
            static_cast<unsigned long long>(number) <= std::numeric_limits<std::uint32_t>::max())
            return static_cast<std::uint32_t>(number);
    }
    return std::nullopt;
}
} // namespace

std::optional<std::size_t> receiverIIP3LevelCount(const ReceiverIIP3TestSettings &settings) {
    if (!std::isfinite(settings.input_start_dBm) || !std::isfinite(settings.input_stop_dBm) ||
        !std::isfinite(settings.input_step_dB) || settings.input_step_dB <= 0.0 ||
        settings.input_start_dBm >= settings.input_stop_dBm)
        return std::nullopt;

    std::size_t count = 0;
    double previous = settings.input_start_dBm;
    for (; count <= kMaxReceiverIIP3InputLevels; ++count) {
        const double level =
            settings.input_start_dBm + static_cast<double>(count) * settings.input_step_dB;
        if (!std::isfinite(level))
            return std::nullopt;
        if (level > settings.input_stop_dBm)
            break;
        if (count > 0 && level <= previous)
            return std::nullopt;
        previous = level;
    }
    if (count < 3 || count > kMaxReceiverIIP3InputLevels)
        return std::nullopt;
    return count;
}

std::optional<std::string>
validateReceiverRequirementsConfig(const ReceiverRequirementsConfig &config) {
    if (!std::isfinite(config.band_start_Hz) || !std::isfinite(config.band_stop_Hz))
        return "Requirement band endpoints must be finite.";
    if (config.band_start_Hz >= config.band_stop_Hz)
        return "Requirement band start must be below band stop.";
    if (config.gain &&
        (!std::isfinite(config.gain->minimum_dB) || !std::isfinite(config.gain->maximum_dB)))
        return "Gain limits must be finite.";
    if (config.gain && config.gain->minimum_dB > config.gain->maximum_dB)
        return "Minimum gain must not exceed maximum gain.";
    if (config.nf_max_dB && !std::isfinite(*config.nf_max_dB))
        return "Noise figure limit must be finite.";
    if (config.output_power && (!std::isfinite(config.output_power->minimum_dBm) ||
                                !std::isfinite(config.output_power->maximum_dBm)))
        return "Output power limits must be finite.";
    if (config.output_power && config.output_power->minimum_dBm > config.output_power->maximum_dBm)
        return "Minimum output power must not exceed maximum output power.";
    if (config.iip3_min_dBm && !std::isfinite(*config.iip3_min_dBm))
        return "IIP3 limit must be finite.";
    if (config.iip3_min_dBm && !config.measurement_conditions.iip3)
        return "IIP3 test settings are required when the IIP3 metric is enabled.";
    if (config.measurement_conditions.iip3) {
        const auto &settings = *config.measurement_conditions.iip3;
        if (!std::isfinite(settings.tone_spacing_Hz) || settings.tone_spacing_Hz <= 0.0)
            return "IIP3 tone spacing must be finite and positive.";
        const auto count = receiverIIP3LevelCount(settings);
        if (!count)
            return "IIP3 input sweep must have 3–101 finite, advancing levels.";
    }
    if (config.measurement_conditions.input_level_dBm &&
        (!std::isfinite(*config.measurement_conditions.input_level_dBm) ||
         *config.measurement_conditions.input_level_dBm < MIN_POWER ||
         *config.measurement_conditions.input_level_dBm > MAX_POWER))
        return "Input level must be between -174 and 10 dBm.";
    return std::nullopt;
}

std::optional<std::string>
receiverRequirementsModeNameError(const ReceiverRequirementsSet &set, std::string_view trimmed_name,
                                  std::optional<std::size_t> except_index) {
    if (trimmed_name.empty())
        return "Mode name must not be empty.";
    if (trimmed_name.size() > kMaxReceiverRequirementModeNameLength)
        return "Mode name must be at most 64 characters.";
    if (trimmed_name.find('#') != std::string_view::npos)
        return "Mode name must not contain '#'.";
    for (std::size_t i = 0; i < set.modes.size(); ++i) {
        if (except_index && *except_index == i)
            continue;
        if (modeNamesEqual(set.modes[i].name, trimmed_name))
            return "A mode named '" + std::string(trimmed_name) + "' already exists.";
    }
    return std::nullopt;
}

std::optional<std::string> validateReceiverRequirementsSet(const ReceiverRequirementsSet &set) {
    if (set.modes.size() > kMaxReceiverRequirementModes)
        return "At most 64 modes are allowed.";
    for (std::size_t i = 0; i < set.modes.size(); ++i) {
        const std::string &name = set.modes[i].name;
        if (name != trimAsciiWhitespace(name))
            return "Mode name must not have leading or trailing spaces.";
        if (name.empty())
            return "Mode name must not be empty.";
        if (name.size() > kMaxReceiverRequirementModeNameLength)
            return "Mode name must be at most 64 characters.";
        if (name.find('#') != std::string::npos)
            return "Mode name must not contain '#'.";
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (modeNamesEqual(set.modes[earlier].name, name))
                return "A mode named '" + name + "' already exists.";
        }
    }
    for (std::size_t i = 0; i < set.modes.size(); ++i) {
        const std::uint32_t id = set.modes[i].id;
        if (id == 0)
            continue;
        if (id >= set.next_mode_id)
            return "Mode id " + std::to_string(id) + " is not below next_mode_id " +
                   std::to_string(set.next_mode_id) + ".";
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (set.modes[earlier].id == id)
                return "Mode id " + std::to_string(id) + " is used twice.";
        }
    }
    if (set.modes.empty() ? set.selected != 0 : set.selected >= set.modes.size())
        return "Selected mode is out of range.";
    for (const auto &mode : set.modes) {
        if (!mode.config)
            continue;
        if (auto reason = validateReceiverRequirementsConfig(*mode.config))
            return "Mode '" + mode.name + "': " + *reason;
    }
    return std::nullopt;
}

nlohmann::json receiverRequirementsToJson(const ReceiverRequirementsSet &set) {
    if (!set.invalid_reason.empty())
        return {{"invalid_configuration", true}, {"diagnostic", set.invalid_reason}};
    nlohmann::json modes = nlohmann::json::array();
    for (const auto &mode : set.modes) {
        nlohmann::json encoded_mode = {{"name", mode.name}};
        if (mode.id != 0)
            encoded_mode["id"] = mode.id;
        encoded_mode["config"] = mode.config ? configToJson(*mode.config) : nlohmann::json(nullptr);
        modes.push_back(std::move(encoded_mode));
    }
    return {{"format", kReceiverRequirementsFormat},
            {"selected_mode", set.selected},
            {"next_mode_id", set.next_mode_id},
            {"modes", std::move(modes)}};
}

ReceiverRequirementsSet receiverRequirementsFromJson(const nlohmann::json &value) {
    if (!value.is_object())
        return invalidRequirementsSet("Receiver requirements must be a JSON object.");
    if (value.contains("invalid_configuration")) {
        if (value["invalid_configuration"].is_boolean() &&
            value["invalid_configuration"].get<bool>()) {
            if (value.contains("diagnostic") && value["diagnostic"].is_string() &&
                !value["diagnostic"].get<std::string>().empty())
                return invalidRequirementsSet(value["diagnostic"].get<std::string>());
            return invalidRequirementsSet("Saved receiver requirements are marked invalid.");
        }
        return invalidRequirementsSet("Receiver requirements contain a malformed invalid marker.");
    }

    ReceiverRequirementsSet set;
    if (!value.contains("format")) {
        std::string error;
        auto config = parseConfigObject(value, error);
        if (!config)
            return invalidRequirementsSet(std::move(error));
        set.modes.push_back({"Default", std::move(config), 1});
        set.next_mode_id = 2;
        if (auto reason = validateReceiverRequirementsSet(set))
            return invalidRequirementsSet(std::move(*reason));
        return set;
    }

    std::string format_text;
    bool supported_format = false;
    const auto &format = value["format"];
    if (format.is_number_integer() || format.is_number_unsigned()) {
        const long long format_number = format.get<long long>();
        supported_format = format_number == 2 || format_number == kReceiverRequirementsFormat;
        format_text = std::to_string(format_number);
    } else {
        format_text = format.dump();
    }
    if (!supported_format)
        return invalidRequirementsSet("Unsupported receiver requirements format " + format_text +
                                      ".");
    // Format 3 carries a stable id per mode and the next id to assign; format 2 has neither.
    const bool with_ids = format.get<long long>() == kReceiverRequirementsFormat;

    if (!value.contains("modes") || !value["modes"].is_array())
        return invalidRequirementsSet("Receiver requirements modes must be a list.");

    std::size_t selected = 0;
    if (value.contains("selected_mode")) {
        const auto &json_selected = value["selected_mode"];
        if (!json_selected.is_number_integer() && !json_selected.is_number_unsigned())
            return invalidRequirementsSet("Selected mode must be a non-negative integer.");
        const long long selected_number = json_selected.get<long long>();
        if (selected_number < 0)
            return invalidRequirementsSet("Selected mode must be a non-negative integer.");
        selected = static_cast<std::size_t>(selected_number);
    }
    set.selected = selected;
    if (with_ids) {
        const auto next_id = positiveModeId(value.value("next_mode_id", nlohmann::json()));
        if (!next_id)
            return invalidRequirementsSet("Next mode id must be a positive integer.");
        set.next_mode_id = *next_id;
    }

    std::size_t index = 0;
    for (const auto &json_mode : value["modes"]) {
        const std::string prefix = "Mode " + std::to_string(index + 1) + ": ";
        ++index;
        if (!json_mode.is_object())
            return invalidRequirementsSet(prefix + "Mode must be a JSON object.");
        if (!json_mode.contains("name") || !json_mode["name"].is_string())
            return invalidRequirementsSet(prefix + "Mode name must be a string.");
        ReceiverRequirementsMode mode;
        mode.name = json_mode["name"].get<std::string>();
        if (with_ids) {
            const auto id =
                json_mode.contains("id") ? positiveModeId(json_mode["id"]) : std::nullopt;
            if (!id)
                return invalidRequirementsSet(prefix + "Mode id must be a positive integer.");
            mode.id = *id;
        }
        if (json_mode.contains("config") && !json_mode["config"].is_null()) {
            std::string error;
            mode.config = parseConfigObject(json_mode["config"], error);
            if (!mode.config)
                return invalidRequirementsSet(prefix + error);
        }
        set.modes.push_back(std::move(mode));
    }
    if (!with_ids)
        assignReceiverRequirementsModeIds(set);
    if (auto reason = validateReceiverRequirementsSet(set))
        return invalidRequirementsSet(std::move(*reason));
    return set;
}

void assignReceiverRequirementsModeIds(ReceiverRequirementsSet &set) {
    for (const auto &mode : set.modes) {
        if (mode.id >= set.next_mode_id)
            set.next_mode_id = mode.id + 1;
    }
    for (auto &mode : set.modes) {
        if (mode.id == 0)
            mode.id = set.next_mode_id++;
    }
}

const ReceiverRequirementsMode *findReceiverRequirementsMode(const ReceiverRequirementsSet &set,
                                                             std::uint32_t id) {
    for (const auto &mode : set.modes) {
        if (mode.id == id)
            return &mode;
    }
    return nullptr;
}

std::optional<ReceiverRequirementsSet>
receiverRequirementsFromEdit(const nlohmann::json &value, const ReceiverRequirementsSet &current,
                             std::string &error_path, std::string &error) {
    error_path = "/requirements";
    error.clear();
    const bool with_ids = value.contains("format") && value["format"].is_number_integer() &&
                          value["format"].get<long long>() == kReceiverRequirementsFormat;
    if (!value.contains("modes") || !value["modes"].is_array()) {
        error = "Receiver requirements modes must be a list.";
        return std::nullopt;
    }
    ReceiverRequirementsSet set;
    set.next_mode_id = current.next_mode_id;
    if (value.contains("selected_mode")) {
        const auto &json_selected = value["selected_mode"];
        if (!json_selected.is_number_integer() && !json_selected.is_number_unsigned()) {
            error = "Selected mode must be a non-negative integer.";
            return std::nullopt;
        }
        const long long selected_number = json_selected.get<long long>();
        if (selected_number < 0) {
            error = "Selected mode must be a non-negative integer.";
            return std::nullopt;
        }
        set.selected = static_cast<std::size_t>(selected_number);
    }
    std::vector<std::uint32_t> used;
    std::size_t index = 0;
    for (const auto &json_mode : value["modes"]) {
        const std::string prefix = "Mode " + std::to_string(index + 1) + ": ";
        const std::string mode_path = "/requirements/modes/" + std::to_string(index);
        ++index;
        if (!json_mode.is_object()) {
            error = prefix + "Mode must be a JSON object.";
            return std::nullopt;
        }
        if (!json_mode.contains("name") || !json_mode["name"].is_string()) {
            error = prefix + "Mode name must be a string.";
            return std::nullopt;
        }
        ReceiverRequirementsMode mode;
        mode.name = json_mode["name"].get<std::string>();
        if (with_ids && json_mode.contains("id")) {
            const auto id = positiveModeId(json_mode["id"]);
            if (!id) {
                error_path = mode_path + "/id";
                error = "Mode id must be a positive integer.";
                return std::nullopt;
            }
            if (!findReceiverRequirementsMode(current, *id)) {
                error_path = mode_path + "/id";
                error = "no mode has id " + std::to_string(*id) + "; omit id to add a mode";
                return std::nullopt;
            }
            if (std::find(used.begin(), used.end(), *id) != used.end()) {
                error_path = mode_path + "/id";
                error = "id " + std::to_string(*id) + " is used twice";
                return std::nullopt;
            }
            used.push_back(*id);
            mode.id = *id;
        } else if (!with_ids) {
            // Format 2 has no ids: a mode keeps the id of the stored mode with the same name.
            for (const auto &stored : current.modes) {
                if (modeNamesEqual(stored.name, mode.name) &&
                    std::find(used.begin(), used.end(), stored.id) == used.end()) {
                    mode.id = stored.id;
                    used.push_back(stored.id);
                    break;
                }
            }
        }
        if (json_mode.contains("config") && !json_mode["config"].is_null()) {
            std::string config_error;
            mode.config = parseConfigObject(json_mode["config"], config_error);
            if (!mode.config) {
                error = prefix + config_error;
                return std::nullopt;
            }
        }
        set.modes.push_back(std::move(mode));
    }
    assignReceiverRequirementsModeIds(set);
    if (auto reason = validateReceiverRequirementsSet(set)) {
        error = std::move(*reason);
        return std::nullopt;
    }
    return set;
}

bool addReceiverRequirementsMode(ReceiverRequirementsSet &set, std::string_view name,
                                 std::string &error) {
    if (set.modes.size() >= kMaxReceiverRequirementModes) {
        error = "At most 64 modes are allowed.";
        return false;
    }
    const std::string trimmed = trimAsciiWhitespace(name);
    if (auto reason = receiverRequirementsModeNameError(set, trimmed, std::nullopt)) {
        error = *reason;
        return false;
    }
    assignReceiverRequirementsModeIds(set);
    ReceiverRequirementsMode mode;
    mode.name = trimmed;
    mode.id = set.next_mode_id++;
    if (!set.modes.empty() && set.selected < set.modes.size())
        mode.config = set.modes[set.selected].config;
    set.modes.push_back(std::move(mode));
    set.selected = set.modes.size() - 1;
    set.invalid_reason.clear();
    error.clear();
    return true;
}

bool renameReceiverRequirementsMode(ReceiverRequirementsSet &set, std::size_t index,
                                    std::string_view name, std::string &error) {
    if (index >= set.modes.size()) {
        error = "Selected mode is out of range.";
        return false;
    }
    const std::string trimmed = trimAsciiWhitespace(name);
    if (auto reason = receiverRequirementsModeNameError(set, trimmed, index)) {
        error = *reason;
        return false;
    }
    set.modes[index].name = trimmed;
    set.invalid_reason.clear();
    error.clear();
    return true;
}

bool removeReceiverRequirementsMode(ReceiverRequirementsSet &set, std::size_t index) {
    if (index >= set.modes.size())
        return false;
    set.modes.erase(set.modes.begin() + static_cast<std::ptrdiff_t>(index));
    if (set.modes.empty())
        set.selected = 0;
    else if (set.selected > index)
        --set.selected;
    else if (set.selected >= set.modes.size())
        set.selected = set.modes.size() - 1;
    set.invalid_reason.clear();
    return true;
}

bool selectReceiverRequirementsMode(ReceiverRequirementsSet &set, std::size_t index) {
    if (index >= set.modes.size())
        return false;
    set.selected = index;
    set.invalid_reason.clear();
    return true;
}

bool applyReceiverRequirementsDraftToSelectedMode(ReceiverRequirementsSet &set,
                                                  const ReceiverRequirementsDraft &draft,
                                                  std::string &error) {
    if (set.modes.empty()) {
        error = "Add a mode before applying requirements.";
        return false;
    }
    if (set.selected >= set.modes.size()) {
        error = "Selected mode is out of range.";
        return false;
    }
    ReceiverRequirementsState state;
    state.config = set.modes[set.selected].config;
    if (!applyReceiverRequirementsDraft(state, draft, error))
        return false;
    set.modes[set.selected].config = state.config;
    set.invalid_reason.clear();
    return true;
}

ReceiverRequirementsState selectedReceiverRequirementsState(const ReceiverRequirementsSet &set) {
    ReceiverRequirementsState state;
    if (!set.invalid_reason.empty())
        state.invalid_reason = set.invalid_reason;
    else if (set.selected < set.modes.size())
        state.config = set.modes[set.selected].config;
    return state;
}

std::optional<ReceiverRequirementsConfig>
parseReceiverRequirementsDraft(const ReceiverRequirementsDraft &draft, std::string &error) {
    const auto band_start = parseNumber(draft.band_start_Hz);
    const auto band_stop = parseNumber(draft.band_stop_Hz);
    if (!band_start || !band_stop) {
        error = "Requirement band endpoints must be finite numbers.";
        return std::nullopt;
    }
    ReceiverRequirementsConfig config;
    config.band_start_Hz = *band_start;
    config.band_stop_Hz = *band_stop;

    auto required = [&](const std::string &text, const char *label) -> std::optional<double> {
        auto value = parseNumber(text);
        if (!value)
            error = std::string(label) + " must be a finite number.";
        return value;
    };
    if (draft.gain_enabled) {
        auto minimum = required(draft.gain_min_dB, "Gain minimum");
        auto maximum = required(draft.gain_max_dB, "Gain maximum");
        if (!minimum || !maximum)
            return std::nullopt;
        config.gain = ReceiverGainLimits{*minimum, *maximum};
    }
    if (draft.noise_figure_enabled) {
        auto maximum = required(draft.nf_max_dB, "Noise figure maximum");
        if (!maximum)
            return std::nullopt;
        config.nf_max_dB = *maximum;
    }
    if (draft.output_power_enabled) {
        auto minimum = required(draft.output_power_min_dBm, "Output power minimum");
        auto maximum = required(draft.output_power_max_dBm, "Output power maximum");
        if (!minimum || !maximum)
            return std::nullopt;
        config.output_power = ReceiverOutputPowerLimits{*minimum, *maximum};
    }
    if (draft.iip3_enabled) {
        auto minimum = required(draft.iip3_min_dBm, "IIP3 minimum");
        auto spacing = required(draft.iip3_tone_spacing_Hz, "IIP3 tone spacing");
        auto start = required(draft.iip3_input_start_dBm, "IIP3 input start");
        auto stop = required(draft.iip3_input_stop_dBm, "IIP3 input stop");
        auto step = required(draft.iip3_input_step_dB, "IIP3 input step");
        if (!minimum || !spacing || !start || !stop || !step)
            return std::nullopt;
        config.iip3_min_dBm = *minimum;
        config.measurement_conditions.iip3 =
            ReceiverIIP3TestSettings{*spacing, *start, *stop, *step};
    }
    if (!draft.input_level_dBm.empty()) {
        auto level = parseNumber(draft.input_level_dBm);
        if (!level) {
            error = "Input level must be a finite number.";
            return std::nullopt;
        }
        config.measurement_conditions.input_level_dBm = *level;
    } else if (draft.output_power_enabled) {
        error = "Output power needs an input level (dBm).";
        return std::nullopt;
    }
    if (auto reason = validateReceiverRequirementsConfig(config)) {
        error = *reason;
        return std::nullopt;
    }
    error.clear();
    return config;
}

bool applyReceiverRequirementsDraft(ReceiverRequirementsState &state,
                                    const ReceiverRequirementsDraft &draft, std::string &error) {
    auto config = parseReceiverRequirementsDraft(draft, error);
    if (!config)
        return false;
    if (!config->gain && !config->nf_max_dB && !config->output_power && !config->iip3_min_dBm)
        state.config.reset();
    else
        state.config = *config;
    state.invalid_reason.clear();
    return true;
}

ReceiverRequirementsEvaluation evaluateReceiverRequirements(
    const ReceiverRequirementsState &state, double sweep_start_Hz, double sweep_stop_Hz,
    const std::vector<double> &frequencies_Hz, const std::vector<double> &gain_dB,
    const std::vector<double> &noise_figure_dB, const std::vector<double> &output_power_dBm,
    const std::vector<double> &iip3_dBm) {
    ReceiverRequirementsEvaluation result;
    if (!state.invalid_reason.empty()) {
        result.gain = result.noise_figure = result.output_power = result.iip3 =
            unavailable(ReceiverRequirementStatus::InvalidConfiguration);
        result.overall = ReceiverRequirementStatus::InvalidConfiguration;
        return result;
    }
    if (!state.config) {
        result.gain = result.noise_figure = result.output_power = result.iip3 =
            unavailable(ReceiverRequirementStatus::NotConfigured);
        result.overall = ReceiverRequirementStatus::NotConfigured;
        return result;
    }
    const auto &config = *state.config;
    if (auto reason = validateReceiverRequirementsConfig(config)) {
        result.gain = result.noise_figure = result.output_power = result.iip3 =
            unavailable(ReceiverRequirementStatus::InvalidConfiguration);
        result.overall = ReceiverRequirementStatus::InvalidConfiguration;
        return result;
    }
    result.gain = config.gain ? unavailable(ReceiverRequirementStatus::Incomplete)
                              : unavailable(ReceiverRequirementStatus::NotConfigured);
    result.noise_figure = config.nf_max_dB ? unavailable(ReceiverRequirementStatus::Incomplete)
                                           : unavailable(ReceiverRequirementStatus::NotConfigured);
    result.output_power = config.output_power
                              ? unavailable(ReceiverRequirementStatus::Incomplete)
                              : unavailable(ReceiverRequirementStatus::NotConfigured);
    result.iip3 = config.iip3_min_dBm ? unavailable(ReceiverRequirementStatus::Incomplete)
                                      : unavailable(ReceiverRequirementStatus::NotConfigured);

    const bool sweep_valid = std::isfinite(sweep_start_Hz) && std::isfinite(sweep_stop_Hz) &&
                             sweep_start_Hz <= sweep_stop_Hz;
    const bool band_covered = sweep_valid && sweep_start_Hz <= config.band_start_Hz &&
                              sweep_stop_Hz >= config.band_stop_Hz;
    if (config.gain)
        result.gain = assess(frequencies_Hz, gain_dB, config.band_start_Hz, config.band_stop_Hz,
                             config.gain->minimum_dB, config.gain->maximum_dB, band_covered);
    if (config.nf_max_dB)
        result.noise_figure =
            assess(frequencies_Hz, noise_figure_dB, config.band_start_Hz, config.band_stop_Hz,
                   std::nullopt, *config.nf_max_dB, band_covered);
    if (config.output_power)
        result.output_power = assess(frequencies_Hz, output_power_dBm, config.band_start_Hz,
                                     config.band_stop_Hz, config.output_power->minimum_dBm,
                                     config.output_power->maximum_dBm, band_covered);
    if (config.iip3_min_dBm)
        result.iip3 = assess(frequencies_Hz, iip3_dBm, config.band_start_Hz, config.band_stop_Hz,
                             *config.iip3_min_dBm, std::nullopt, band_covered);
    result.overall = aggregate(result);
    return result;
}
