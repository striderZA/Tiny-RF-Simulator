#include "rf_budget_stage.h"

#include "amplifier_engine.h"
#include "component_library.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "node_graph_engine.h"
#include "sweep_response.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace {

constexpr double kCombPower_dBm = -30.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// A private graph/registry pair for one stage engine, as the Network Analyzer's NaScratch is.
struct StageScratch {
    NodeGraphEngine graph;
    ViewManager view;
    ComponentRegistry registry{graph, view};

    IComponentEngine *build(std::string_view type) {
        const auto *descriptor = ComponentTypeRegistry::instance().find(type);
        if (!descriptor || !descriptor->create)
            return nullptr;
        return descriptor->create(registry, graph, 1);
    }
};

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

std::string join(const std::vector<std::string> &sentences) {
    std::string joined;
    for (const auto &sentence : sentences)
        joined += (joined.empty() ? "" : " ") + sentence;
    return joined;
}

int stageOutputCount(std::string_view type) {
    for (const auto &entry : rfBudgetStageTypes()) {
        if (entry.type == type)
            return entry.outputs;
    }
    return 1;
}

bool isSwitchType(std::string_view type) {
    return type == "rf_switch_spdt" || type == "rf_switch_spdt_2to1";
}

std::string escapePointerToken(const std::string &token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '~')
            escaped += "~0";
        else if (character == '/')
            escaped += "~1";
        else
            escaped.push_back(character);
    }
    return escaped;
}

RfBudgetSeries makeSeries(std::size_t count, double value, std::uint16_t reason) {
    RfBudgetSeries series;
    series.values.assign(count, value);
    series.reasons.assign(count, reason);
    return series;
}

nlohmann::ordered_json overrideParams(const std::map<std::string, double> &overrides) {
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    for (const auto &[key, value] : overrides)
        params[key] = value;
    return params;
}

} // namespace

std::uint16_t RfBudgetReasonTable::add(const std::string &reason) {
    for (std::size_t index = 0; index < m_reasons.size(); ++index) {
        if (m_reasons[index] == reason)
            return static_cast<std::uint16_t>(index);
    }
    m_reasons.push_back(reason);
    return static_cast<std::uint16_t>(m_reasons.size() - 1);
}

const std::string &RfBudgetReasonTable::at(std::uint16_t i) const {
    static const std::string empty;
    return i < m_reasons.size() ? m_reasons[i] : empty;
}

struct RfBudgetStageEvaluator::CacheEntry {
    std::string stage;
    std::map<std::string, double> overrides;
    std::unique_ptr<StageScratch> scratch;
    IComponentEngine *engine = nullptr;
    AmplifierEngine *amplifier = nullptr;
    std::uint16_t stage_reason = 0;
    int output_count = 1;
    int active_throw = -1;
    bool nonlinear = false;
};

std::unique_ptr<RfBudgetStageEvaluator::CacheEntry>
RfBudgetStageEvaluator::buildEntry(const RfBudgetStage &stage,
                                   const std::map<std::string, double> &overrides) {
    auto entry = std::make_unique<CacheEntry>();
    entry->stage = stage.name;
    entry->overrides = overrides;
    entry->output_count = stageOutputCount(stage.type);

    entry->scratch = std::make_unique<StageScratch>();
    const auto *descriptor = ComponentTypeRegistry::instance().find(stage.type);
    IComponentEngine *engine =
        descriptor ? entry->scratch->build(stage.type) : static_cast<IComponentEngine *>(nullptr);
    if (!engine) {
        entry->stage_reason =
            m_reasons.add("Stage type '" + stage.type + "' is not supported in a budget.");
        return entry;
    }
    entry->engine = engine;

    if (stage.source == RfBudgetStageSource::Library) {
        std::vector<const ComponentDefinition *> matches;
        for (const ComponentDefinition *definition : m_library.byType(stage.type)) {
            if (definition && equalsIgnoreCase(definition->part_number, stage.part_number))
                matches.push_back(definition);
        }
        if (matches.empty()) {
            entry->stage_reason = m_reasons.add("Library part '" + stage.part_number + "' (" +
                                                stage.type + ") is not in the component library.");
        } else if (matches.size() > 1) {
            entry->stage_reason =
                m_reasons.add("Library part '" + stage.part_number + "' (" + stage.type +
                              ") matches " + std::to_string(matches.size()) + " library entries.");
        } else {
            const std::vector<std::string> issues =
                m_library.applyDefinition(*matches.front(), *engine);
            if (!issues.empty())
                entry->stage_reason = m_reasons.add(join(issues));
        }
    } else {
        const ParamWriteResult result =
            applyComponentParams(*engine, descriptor->state_fields, stage.params);
        if (!result.ok()) {
            entry->stage_reason = m_reasons.add("Stage '" + stage.name +
                                                "' params refused: " + paramWriteMessage(result));
        }
    }

    if (entry->stage_reason == 0 && !overrides.empty()) {
        const ParamWriteResult result =
            applyComponentParams(*engine, descriptor->state_fields, overrideParams(overrides));
        if (!result.ok()) {
            entry->stage_reason = m_reasons.add("Mode override '" + stage.name + "." + result.path +
                                                "' refused: " + paramWriteMessage(result));
        }
    }

    if (entry->stage_reason == 0 && isSwitchType(stage.type)) {
        const nlohmann::json serialized = engine->serialize();
        if (serialized.is_object())
            entry->active_throw = serialized.value("active_throw", -1);
    }

    entry->amplifier = dynamic_cast<AmplifierEngine *>(engine);
    if (entry->amplifier) {
        // Record the configured flag, then run the comb linearly: the gain and NF series carry no
        // compression. oip3At_Hz() is independent of the flag, so the recorded value survives.
        entry->nonlinear = entry->amplifier->enableNonlinear();
        entry->amplifier->setEnableNonlinear(false);
    }
    return entry;
}

RfBudgetStageEvaluator::RfBudgetStageEvaluator(const ComponentLibrary &library,
                                               RfBudgetReasonTable &reasons)
    : m_library(library), m_reasons(reasons) {}

RfBudgetStageEvaluator::~RfBudgetStageEvaluator() = default;

RfBudgetStageResponse
RfBudgetStageEvaluator::evaluate(const RfBudgetStage &stage,
                                 const std::map<std::string, double> *overrides,
                                 const std::vector<double> &frequencies_Hz, int input_port) {
    static const std::map<std::string, double> kNoOverrides;
    const std::map<std::string, double> &wanted = overrides ? *overrides : kNoOverrides;

    CacheEntry *entry = nullptr;
    for (auto &candidate : m_cache) {
        if (candidate->stage == stage.name && candidate->overrides == wanted) {
            entry = candidate.get();
            break;
        }
    }
    if (!entry) {
        m_cache.push_back(buildEntry(stage, wanted));
        entry = m_cache.back().get();
    }

    RfBudgetStageResponse response;
    response.stage = stage.name;
    response.stage_reason = entry->stage_reason;
    response.active_throw = entry->active_throw;

    const std::size_t point_count = frequencies_Hz.size();
    const auto notBuilt = [&](std::uint16_t reason) {
        for (int port = 0; port < entry->output_count; ++port) {
            response.gain_dB.push_back(makeSeries(point_count, kNaN, reason));
            response.nf_dB.push_back(makeSeries(point_count, kNaN, reason));
        }
        response.oip3_dBm = makeSeries(point_count, kNaN, reason);
        return response;
    };
    if (entry->stage_reason != 0)
        return notBuilt(entry->stage_reason);

    auto &inputs = entry->engine->node().inputs;
    if (input_port < 0 || static_cast<std::size_t>(input_port) >= inputs.size()) {
        response.stage_reason = m_reasons.add("Stage '" + stage.name + "' has no input port " +
                                              std::to_string(input_port) + ".");
        return notBuilt(response.stage_reason);
    }

    // OIP3 comes from the amplifier's own intercept chain; passives are linear by contract.
    if (entry->amplifier && entry->nonlinear) {
        response.oip3_dBm.values.resize(point_count);
        response.oip3_dBm.reasons.resize(point_count);
        for (std::size_t i = 0; i < point_count; ++i) {
            const AmplifierInterceptAt at = entry->amplifier->oip3At_Hz(frequencies_Hz[i]);
            if (at.covered) {
                response.oip3_dBm.values[i] = at.oip3_dBm;
                response.oip3_dBm.reasons[i] = 0;
            } else {
                response.oip3_dBm.values[i] = kNaN;
                response.oip3_dBm.reasons[i] =
                    m_reasons.add("Stage '" + stage.name + "': " + at.reason);
            }
        }
    } else {
        const std::uint16_t reason = m_reasons.add(
            entry->amplifier ? "Stage '" + stage.name + "' is linear (nonlinearity off)."
                             : "Stage '" + stage.name + "' is linear.");
        response.oip3_dBm = makeSeries(point_count, kInf, reason);
    }

    Spectrum stimulus = sweepStimulus(frequencies_Hz, kCombPower_dBm);
    inputs[static_cast<std::size_t>(input_port)] = &stimulus;
    entry->engine->update(0.0);
    inputs[static_cast<std::size_t>(input_port)] = nullptr;

    const auto &outputs = entry->engine->node().outputs;
    for (std::size_t port = 0; port < outputs.size(); ++port) {
        const std::vector<SweepPointResponse> points =
            readSweepResponse(frequencies_Hz, outputs[port]);
        RfBudgetSeries gain, nf;
        gain.values.reserve(point_count);
        gain.reasons.reserve(point_count);
        nf.values.reserve(point_count);
        nf.reasons.reserve(point_count);
        for (std::size_t i = 0; i < point_count; ++i) {
            const SweepPointResponse &point = points[i];
            if (point.tone_out_of_band || point.noise_out_of_band) {
                const std::uint16_t reason = m_reasons.add(
                    "Stage '" + stage.name + "': S-parameter data does not cover this frequency.");
                gain.values.push_back(kNaN);
                gain.reasons.push_back(reason);
                nf.values.push_back(kNaN);
                nf.reasons.push_back(reason);
                continue;
            }
            if (!point.tone_dBm) {
                const std::uint16_t reason =
                    m_reasons.add("Stage '" + stage.name + "' passes no signal at this frequency.");
                gain.values.push_back(-kInf);
                gain.reasons.push_back(reason);
                nf.values.push_back(kInf);
                nf.reasons.push_back(reason);
                continue;
            }
            const double gain_dB = *point.tone_dBm - kCombPower_dBm;
            gain.values.push_back(gain_dB);
            gain.reasons.push_back(0);
            const double nf_dB =
                point.noise_total_W ? sweepNoiseFigure_dB(*point.noise_total_W, gain_dB) : kNaN;
            if (std::isnan(nf_dB)) {
                nf.values.push_back(kNaN);
                nf.reasons.push_back(m_reasons.add(
                    "Stage '" + stage.name + "' noise figure is undefined at this frequency."));
            } else {
                nf.values.push_back(nf_dB);
                nf.reasons.push_back(0);
            }
        }
        response.gain_dB.push_back(std::move(gain));
        response.nf_dB.push_back(std::move(nf));
    }
    return response;
}

std::optional<RfBudgetParamIssue> checkRfBudgetParams(const RfBudget &budget) {
    // One scratch engine per stage: idealized params first, then every mode record's overrides.
    // A library stage applies its data at evaluation time, so its overrides are checked against
    // the default engine of the same type.
    struct StageCheck {
        const RfBudgetStage *stage = nullptr;
        std::unique_ptr<StageScratch> scratch;
        const ComponentTypeDescriptor *descriptor = nullptr;
        IComponentEngine *engine = nullptr;
    };
    std::vector<StageCheck> checks;
    checks.reserve(budget.stages.size());
    for (std::size_t index = 0; index < budget.stages.size(); ++index) {
        const RfBudgetStage &stage = budget.stages[index];
        StageCheck check;
        check.stage = &stage;
        check.descriptor = ComponentTypeRegistry::instance().find(stage.type);
        check.scratch = std::make_unique<StageScratch>();
        check.engine = check.descriptor ? check.scratch->build(stage.type) : nullptr;
        if (check.engine && stage.source == RfBudgetStageSource::Idealized) {
            const ParamWriteResult result =
                applyComponentParams(*check.engine, check.descriptor->state_fields, stage.params);
            if (!result.ok()) {
                return RfBudgetParamIssue{"/budget/stages/" + std::to_string(index) + "/params",
                                          result};
            }
        }
        checks.push_back(std::move(check));
    }

    for (std::size_t mode_index = 0; mode_index < budget.modes.size(); ++mode_index) {
        const auto &overrides = budget.modes[mode_index].overrides;
        for (const auto &[stage_name, values] : overrides) {
            const auto check =
                std::find_if(checks.begin(), checks.end(), [&](const StageCheck &candidate) {
                    return candidate.stage->name == stage_name;
                });
            if (check == checks.end() || !check->engine)
                continue;
            const ParamWriteResult result = applyComponentParams(
                *check->engine, check->descriptor->state_fields, overrideParams(values));
            if (!result.ok()) {
                return RfBudgetParamIssue{"/budget/modes/" + std::to_string(mode_index) +
                                              "/overrides/" + escapePointerToken(stage_name),
                                          result};
            }
        }
    }
    return std::nullopt;
}
