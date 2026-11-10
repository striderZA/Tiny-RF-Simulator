#include "rf_budget.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace {

constexpr std::size_t kMaxRfBudgetNameLength = 64;

char asciiLower(char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

bool namesEqual(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (asciiLower(left[i]) != asciiLower(right[i]))
            return false;
    }
    return true;
}

std::string trimAsciiWhitespace(const std::string &text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\n' ||
                           text[begin] == '\r'))
        ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' ||
                           text[end - 1] == '\r'))
        --end;
    return text.substr(begin, end - begin);
}

const RfBudgetStageType *stageTypeInfo(std::string_view type) {
    for (const auto &entry : rfBudgetStageTypes()) {
        if (entry.type == type)
            return &entry;
    }
    return nullptr;
}

bool hasParams(const nlohmann::ordered_json &params) {
    return !params.is_null() && !params.empty();
}

std::optional<std::string> nameError(const std::string &name, const char *kind) {
    const std::string label = std::string(kind) + " name ";
    if (name != trimAsciiWhitespace(name))
        return label + "must not have leading or trailing spaces.";
    if (name.empty())
        return label + "must not be empty.";
    if (name.size() > kMaxRfBudgetNameLength)
        return label + "must be at most 64 characters.";
    if (name.find('#') != std::string::npos)
        return label + "must not contain '#'.";
    return std::nullopt;
}

std::optional<std::size_t> findStageIndex(const RfBudget &budget, const std::string &name) {
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        if (namesEqual(budget.stages[i].name, name))
            return i;
    }
    return std::nullopt;
}

RfBudget invalidBudget(const std::string &reason) {
    RfBudget budget;
    budget.invalid_reason = reason;
    return budget;
}

// Object key order is not part of a parameter's meaning; compare by content.
bool jsonContentEqual(const nlohmann::ordered_json &left, const nlohmann::ordered_json &right) {
    // A null parameter set and an empty object both mean "no parameters".
    const auto absent = [](const nlohmann::ordered_json &value) {
        return value.is_null() || (value.is_object() && value.empty());
    };
    if (absent(left) && absent(right))
        return true;
    if (left.type() != right.type())
        return false;
    if (left.is_object()) {
        if (left.size() != right.size())
            return false;
        for (auto it = left.begin(); it != left.end(); ++it) {
            const auto other = right.find(it.key());
            if (other == right.end() || !jsonContentEqual(it.value(), *other))
                return false;
        }
        return true;
    }
    if (left.is_array()) {
        if (left.size() != right.size())
            return false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (!jsonContentEqual(left[i], right[i]))
                return false;
        }
        return true;
    }
    return left == right;
}

bool nonNegativeInteger(const nlohmann::json &value, long long &out) {
    if (!value.is_number_integer() && !value.is_number_unsigned())
        return false;
    if (value.is_number_unsigned()) {
        const auto unsigned_value = value.get<unsigned long long>();
        if (unsigned_value > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
            return false;
        out = static_cast<long long>(unsigned_value);
        return out >= 0;
    }
    out = value.get<long long>();
    return out >= 0;
}

} // namespace

const std::vector<RfBudgetStageType> &rfBudgetStageTypes() {
    static const std::vector<RfBudgetStageType> types = {
        {"amplifier", 1, 1},      {"attenuator", 1, 1},          {"filter", 1, 1},
        {"coax", 1, 1},           {"equalizer", 1, 1},           {"splitter", 1, 2},
        {"rf_switch_spdt", 1, 2}, {"rf_switch_spdt_2to1", 2, 1},
    };
    return types;
}

bool RfBudgetStage::operator==(const RfBudgetStage &other) const {
    return name == other.name && type == other.type && source == other.source &&
           part_number == other.part_number && inputs == other.inputs &&
           jsonContentEqual(params, other.params);
}

bool RfBudget::empty() const {
    return stages.empty() && outputs.empty() && modes.empty() && invalid_reason.empty();
}

std::optional<std::string> validateRfBudget(const RfBudget &budget) {
    if (budget.stages.size() > kMaxRfBudgetStages)
        return "At most 32 stages are allowed.";
    if (budget.outputs.size() > kMaxRfBudgetOutputs)
        return "At most 8 outputs are allowed.";
    if (budget.modes.size() > kMaxRfBudgetModeRecords)
        return "At most 64 mode records are allowed.";
    if (budget.point_count < kMinRfBudgetPoints || budget.point_count > kMaxRfBudgetPoints)
        return "Point count must be between 2 and 401.";
    if (budget.outputs.empty())
        return "The budget needs at least one output.";

    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        const RfBudgetStage &stage = budget.stages[i];
        if (auto reason = nameError(stage.name, "Stage"))
            return reason;
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (namesEqual(budget.stages[earlier].name, stage.name))
                return "A stage named '" + stage.name + "' already exists.";
        }
    }
    for (std::size_t i = 0; i < budget.outputs.size(); ++i) {
        const RfBudgetOutput &output = budget.outputs[i];
        if (auto reason = nameError(output.name, "Output"))
            return reason;
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (namesEqual(budget.outputs[earlier].name, output.name))
                return "An output named '" + output.name + "' already exists.";
        }
    }

    for (const RfBudgetStage &stage : budget.stages) {
        if (stage.type == "mixer")
            return "Mixer stages are not supported: the budget keeps every signal at its input "
                   "frequency.";
        const RfBudgetStageType *type = stageTypeInfo(stage.type);
        if (!type)
            return "Stage type '" + stage.type + "' is not supported in a budget.";
        if (stage.source == RfBudgetStageSource::Library) {
            if (stage.part_number.empty())
                return "Stage '" + stage.name + "' is a library stage and needs a part number.";
            if (hasParams(stage.params))
                return "Stage '" + stage.name + "' is a library stage and takes no params.";
        } else if (!stage.part_number.empty()) {
            return "Stage '" + stage.name + "' is idealized and takes no part number.";
        }

        const std::size_t declared_inputs = static_cast<std::size_t>(type->inputs);
        if (stage.inputs.size() != declared_inputs)
            return "Stage '" + stage.name + "' needs " + std::to_string(type->inputs) +
                   " input feeds.";
        bool any_fed = false;
        for (std::size_t port = 0; port < stage.inputs.size(); ++port) {
            const std::string port_text = std::to_string(port);
            const std::optional<RfBudgetFeed> &feed = stage.inputs[port];
            if (!feed) {
                if (declared_inputs == 1)
                    return "Stage '" + stage.name + "' input " + port_text + " is not fed.";
                continue;
            }
            any_fed = true;
            if (!feed->stage)
                continue;
            const auto source_index = findStageIndex(budget, *feed->stage);
            if (!source_index)
                return "Stage '" + stage.name + "' input " + port_text +
                       " is fed from unknown stage '" + *feed->stage + "'.";
            const RfBudgetStage &source = budget.stages[*source_index];
            if (namesEqual(source.name, stage.name))
                return "Stage '" + stage.name + "' feeds itself.";
            const RfBudgetStageType *source_type = stageTypeInfo(source.type);
            const int source_outputs = source_type ? source_type->outputs : 0;
            if (feed->port < 0 || feed->port >= source_outputs)
                return "Stage '" + stage.name + "' input " + port_text + " is fed from port " +
                       std::to_string(feed->port) + " of stage '" + source.name + "', which has " +
                       std::to_string(source_outputs) + " outputs.";
        }
        if (declared_inputs > 1 && !any_fed)
            return "Stage '" + stage.name + "' has no fed input.";
    }

    std::size_t input_feeds = 0;
    std::size_t root = budget.stages.size();
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        for (const auto &feed : budget.stages[i].inputs) {
            if (feed && !feed->stage) {
                ++input_feeds;
                root = i;
            }
        }
    }
    if (input_feeds != 1)
        return "Exactly one stage input must be fed from the budget input.";

    // Consumers per output port, over stage feeds and outputs alike, plus the
    // consumer edges used by the loop walk. Outputs that cannot resolve are
    // left out here and refused by the output checks below.
    std::map<std::pair<std::size_t, int>, int> consumption;
    std::vector<std::vector<std::size_t>> consumers(budget.stages.size());
    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        for (const auto &feed : budget.stages[i].inputs) {
            if (!feed || !feed->stage)
                continue;
            const auto source_index = findStageIndex(budget, *feed->stage);
            if (!source_index)
                continue;
            ++consumption[{*source_index, feed->port}];
            consumers[*source_index].push_back(i);
        }
    }
    for (const RfBudgetOutput &output : budget.outputs) {
        const auto stage_index = findStageIndex(budget, output.stage);
        if (!stage_index)
            continue;
        const RfBudgetStageType *type = stageTypeInfo(budget.stages[*stage_index].type);
        const int outputs = type ? type->outputs : 0;
        if (output.port < 0 || output.port >= outputs)
            continue;
        ++consumption[{*stage_index, output.port}];
    }

    enum class Mark { Unvisited, OnPath, Done };
    std::vector<Mark> marks(budget.stages.size(), Mark::Unvisited);
    std::optional<std::string> loop_reason;
    std::function<void(std::size_t)> visit = [&](std::size_t index) {
        marks[index] = Mark::OnPath;
        for (const std::size_t consumer : consumers[index]) {
            if (loop_reason)
                return;
            if (marks[consumer] == Mark::OnPath) {
                loop_reason =
                    "The stages form a loop through '" + budget.stages[consumer].name + "'.";
                return;
            }
            if (marks[consumer] == Mark::Unvisited)
                visit(consumer);
            if (loop_reason)
                return;
        }
        marks[index] = Mark::Done;
    };
    visit(root);
    if (loop_reason)
        return loop_reason;

    // std::map order makes the first over-consumed port deterministic: stage, then port.
    for (const auto &[port, count] : consumption) {
        if (count < 2)
            continue;
        return "Port " + std::to_string(port.second) + " of stage '" +
               budget.stages[port.first].name + "' feeds more than one input or output.";
    }

    for (std::size_t i = 0; i < budget.stages.size(); ++i) {
        if (marks[i] == Mark::Unvisited)
            return "Stage '" + budget.stages[i].name + "' is not reached from the budget input.";
    }

    for (const RfBudgetOutput &output : budget.outputs) {
        const auto stage_index = findStageIndex(budget, output.stage);
        if (!stage_index)
            return "Output '" + output.name + "' reads unknown stage '" + output.stage + "'.";
        const RfBudgetStage &stage = budget.stages[*stage_index];
        const RfBudgetStageType *type = stageTypeInfo(stage.type);
        const int outputs = type ? type->outputs : 0;
        if (output.port < 0 || output.port >= outputs)
            return "Output '" + output.name + "' reads port " + std::to_string(output.port) +
                   " of stage '" + stage.name + "', which has " + std::to_string(outputs) +
                   " outputs.";
    }

    for (std::size_t i = 0; i < budget.modes.size(); ++i) {
        const RfBudgetModeRecord &record = budget.modes[i];
        const std::string mode_text = std::to_string(record.mode_id);
        if (record.mode_id == 0)
            return "Mode id 0 is not valid.";
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (budget.modes[earlier].mode_id == record.mode_id)
                return "Mode id " + mode_text + " has two budget records.";
        }
        for (const auto &[stage_name, overrides] : record.overrides) {
            if (!findStageIndex(budget, stage_name))
                return "Mode " + mode_text + " overrides unknown stage '" + stage_name + "'.";
            for (const auto &[key, value] : overrides) {
                if (!std::isfinite(value))
                    return "Mode " + mode_text + " override '" + stage_name + "." + key +
                           "' must be a finite number.";
            }
        }
    }
    return std::nullopt;
}

nlohmann::json rfBudgetToJson(const RfBudget &budget) {
    if (!budget.invalid_reason.empty())
        return {{"invalid_configuration", true}, {"diagnostic", budget.invalid_reason}};

    nlohmann::json stages = nlohmann::json::array();
    for (const RfBudgetStage &stage : budget.stages) {
        const bool library = stage.source == RfBudgetStageSource::Library;
        nlohmann::json encoded = {{"name", stage.name},
                                  {"type", stage.type},
                                  {"source", library ? "library" : "idealized"}};
        if (library)
            encoded["part_number"] = stage.part_number;
        else
            encoded["params"] =
                stage.params.is_null() ? nlohmann::json::object() : nlohmann::json(stage.params);
        nlohmann::json inputs = nlohmann::json::array();
        for (const auto &feed : stage.inputs) {
            if (!feed) {
                inputs.push_back(nullptr);
                continue;
            }
            if (!feed->stage) {
                inputs.push_back({{"from", "input"}});
                continue;
            }
            inputs.push_back({{"from", "stage"}, {"stage", *feed->stage}, {"port", feed->port}});
        }
        encoded["inputs"] = std::move(inputs);
        stages.push_back(std::move(encoded));
    }

    nlohmann::json outputs = nlohmann::json::array();
    for (const RfBudgetOutput &output : budget.outputs)
        outputs.push_back({{"name", output.name}, {"stage", output.stage}, {"port", output.port}});

    nlohmann::json modes = nlohmann::json::array();
    for (const RfBudgetModeRecord &record : budget.modes) {
        nlohmann::json overrides = nlohmann::json::object();
        for (const auto &[stage_name, values] : record.overrides) {
            nlohmann::json encoded_values = nlohmann::json::object();
            for (const auto &[key, value] : values)
                encoded_values[key] = value;
            overrides[stage_name] = std::move(encoded_values);
        }
        modes.push_back({{"mode_id", record.mode_id}, {"overrides", std::move(overrides)}});
    }

    return {{"format", kRfBudgetFormat},
            {"point_count", budget.point_count},
            {"stages", std::move(stages)},
            {"outputs", std::move(outputs)},
            {"modes", std::move(modes)}};
}

RfBudget rfBudgetFromJson(const nlohmann::json &value) {
    if (!value.is_object())
        return invalidBudget("RF budget must be a JSON object.");
    if (value.contains("invalid_configuration")) {
        if (value["invalid_configuration"].is_boolean() &&
            value["invalid_configuration"].get<bool>()) {
            if (value.contains("diagnostic") && value["diagnostic"].is_string() &&
                !value["diagnostic"].get<std::string>().empty())
                return invalidBudget(value["diagnostic"].get<std::string>());
            return invalidBudget("Saved RF budget is marked invalid.");
        }
        return invalidBudget("RF budget contains a malformed invalid marker.");
    }

    RfBudget budget;
    bool supported_format = false;
    std::string format_text = "null";
    if (value.contains("format")) {
        const auto &format = value["format"];
        if (format.is_number_integer() || format.is_number_unsigned()) {
            const long long number = format.get<long long>();
            supported_format = number == kRfBudgetFormat;
            format_text = std::to_string(number);
        } else {
            format_text = format.dump();
        }
    }
    if (!supported_format)
        return invalidBudget("Unsupported RF budget format " + format_text + ".");

    if (value.contains("point_count")) {
        long long points = 0;
        if (!nonNegativeInteger(value["point_count"], points))
            return invalidBudget("Point count must be an integer.");
        if (points < kMinRfBudgetPoints || points > kMaxRfBudgetPoints)
            return invalidBudget("Point count must be between 2 and 401.");
        budget.point_count = static_cast<int>(points);
    }

    if (value.contains("stages")) {
        if (!value["stages"].is_array())
            return invalidBudget("RF budget stages must be a list.");
        std::size_t stage_index = 0;
        for (const auto &json_stage : value["stages"]) {
            const std::string prefix = "Stage " + std::to_string(stage_index + 1) + ": ";
            ++stage_index;
            if (!json_stage.is_object())
                return invalidBudget(prefix + "Stage must be a JSON object.");
            RfBudgetStage stage;
            if (!json_stage.contains("name") || !json_stage["name"].is_string())
                return invalidBudget(prefix + "Stage name must be a string.");
            stage.name = json_stage["name"].get<std::string>();
            if (!json_stage.contains("type") || !json_stage["type"].is_string())
                return invalidBudget(prefix + "Stage type must be a string.");
            stage.type = json_stage["type"].get<std::string>();
            if (json_stage.contains("source")) {
                if (!json_stage["source"].is_string())
                    return invalidBudget(prefix + "Stage source must be 'library' or 'idealized'.");
                const std::string source = json_stage["source"].get<std::string>();
                if (source == "library") {
                    stage.source = RfBudgetStageSource::Library;
                } else if (source == "idealized") {
                    stage.source = RfBudgetStageSource::Idealized;
                } else {
                    return invalidBudget(prefix + "Stage source must be 'library' or 'idealized'.");
                }
            }
            if (json_stage.contains("part_number")) {
                if (!json_stage["part_number"].is_string())
                    return invalidBudget(prefix + "Part number must be a string.");
                stage.part_number = json_stage["part_number"].get<std::string>();
            }
            if (json_stage.contains("params") && !json_stage["params"].is_null()) {
                if (!json_stage["params"].is_object())
                    return invalidBudget(prefix + "Stage params must be a JSON object.");
                stage.params = nlohmann::ordered_json(json_stage["params"]);
            }
            if (json_stage.contains("inputs") && !json_stage["inputs"].is_null()) {
                if (!json_stage["inputs"].is_array())
                    return invalidBudget(prefix + "Stage inputs must be a list.");
                std::size_t input_index = 0;
                for (const auto &json_input : json_stage["inputs"]) {
                    const std::string input_prefix =
                        prefix + "Stage input " + std::to_string(input_index) + ": ";
                    ++input_index;
                    if (json_input.is_null()) {
                        stage.inputs.push_back(std::nullopt);
                        continue;
                    }
                    if (!json_input.is_object())
                        return invalidBudget(input_prefix + "must be a JSON object.");
                    RfBudgetFeed feed;
                    if (!json_input.contains("from") || !json_input["from"].is_string())
                        return invalidBudget(input_prefix + "'from' must be 'input' or 'stage'.");
                    const std::string from = json_input["from"].get<std::string>();
                    if (from == "input") {
                        feed.stage = std::nullopt;
                    } else if (from == "stage") {
                        if (!json_input.contains("stage") || !json_input["stage"].is_string())
                            return invalidBudget(input_prefix + "stage must be a string.");
                        feed.stage = json_input["stage"].get<std::string>();
                    } else {
                        return invalidBudget(input_prefix + "'from' must be 'input' or 'stage'.");
                    }
                    if (json_input.contains("port")) {
                        long long port = 0;
                        if (!nonNegativeInteger(json_input["port"], port))
                            return invalidBudget(input_prefix +
                                                 "port must be a non-negative integer.");
                        feed.port = static_cast<int>(port);
                    }
                    stage.inputs.push_back(std::move(feed));
                }
            }
            budget.stages.push_back(std::move(stage));
        }
    }

    if (value.contains("outputs")) {
        if (!value["outputs"].is_array())
            return invalidBudget("RF budget outputs must be a list.");
        std::size_t output_index = 0;
        for (const auto &json_output : value["outputs"]) {
            const std::string prefix = "Output " + std::to_string(output_index + 1) + ": ";
            ++output_index;
            if (!json_output.is_object())
                return invalidBudget(prefix + "Output must be a JSON object.");
            RfBudgetOutput output;
            if (!json_output.contains("name") || !json_output["name"].is_string())
                return invalidBudget(prefix + "Output name must be a string.");
            output.name = json_output["name"].get<std::string>();
            if (!json_output.contains("stage") || !json_output["stage"].is_string())
                return invalidBudget(prefix + "Output stage must be a string.");
            output.stage = json_output["stage"].get<std::string>();
            if (json_output.contains("port")) {
                long long port = 0;
                if (!nonNegativeInteger(json_output["port"], port))
                    return invalidBudget(prefix + "Output port must be a non-negative integer.");
                output.port = static_cast<int>(port);
            }
            budget.outputs.push_back(std::move(output));
        }
    }

    if (value.contains("modes")) {
        if (!value["modes"].is_array())
            return invalidBudget("RF budget modes must be a list.");
        std::size_t mode_index = 0;
        for (const auto &json_mode : value["modes"]) {
            const std::string prefix = "Mode " + std::to_string(mode_index + 1) + ": ";
            ++mode_index;
            if (!json_mode.is_object())
                return invalidBudget(prefix + "Mode must be a JSON object.");
            RfBudgetModeRecord record;
            if (json_mode.contains("mode_id")) {
                long long mode_id = 0;
                if (!nonNegativeInteger(json_mode["mode_id"], mode_id) ||
                    mode_id > static_cast<long long>(std::numeric_limits<std::uint32_t>::max()))
                    return invalidBudget(prefix + "Mode id must be a non-negative integer.");
                record.mode_id = static_cast<std::uint32_t>(mode_id);
            }
            if (json_mode.contains("overrides") && !json_mode["overrides"].is_null()) {
                if (!json_mode["overrides"].is_object())
                    return invalidBudget(prefix + "Overrides must be a JSON object.");
                for (auto it = json_mode["overrides"].begin(); it != json_mode["overrides"].end();
                     ++it) {
                    if (!it.value().is_object())
                        return invalidBudget(prefix + "Overrides for '" + it.key() +
                                             "' must be a JSON object.");
                    for (auto value_it = it.value().begin(); value_it != it.value().end();
                         ++value_it) {
                        if (!value_it.value().is_number())
                            return invalidBudget(prefix + "Override '" + it.key() + "." +
                                                 value_it.key() + "' must be a number.");
                        record.overrides[it.key()][value_it.key()] = value_it.value().get<double>();
                    }
                }
            }
            budget.modes.push_back(std::move(record));
        }
    }

    if (auto reason = validateRfBudget(budget))
        return invalidBudget(*reason);
    return budget;
}

const RfBudgetModeRecord *findRfBudgetModeRecord(const RfBudget &budget, std::uint32_t mode_id) {
    for (const auto &record : budget.modes) {
        if (record.mode_id == mode_id)
            return &record;
    }
    return nullptr;
}
