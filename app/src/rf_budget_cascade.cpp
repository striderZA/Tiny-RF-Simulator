#include "rf_budget_cascade.h"

#include "common.h"
#include "nonlinear_model.h"
#include "sweep_response.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
// Below this cumulative gain a readout has no value (the Network Analyzer's floor).
constexpr double kGainFloor_dB = -100.0;

RfBudgetSeries makeSeries(std::size_t count) {
    RfBudgetSeries series;
    series.values.assign(count, kNaN);
    series.reasons.assign(count, 0);
    return series;
}

RfBudgetSeries normalized(const RfBudgetSeries &source, std::size_t count) {
    RfBudgetSeries series = makeSeries(count);
    const std::size_t shared = std::min(count, source.values.size());
    for (std::size_t i = 0; i < shared; ++i) {
        series.values[i] = source.values[i];
        series.reasons[i] = i < source.reasons.size() ? source.reasons[i] : 0;
    }
    return series;
}

bool isSwitchType(std::string_view type) {
    return type == "rf_switch_spdt" || type == "rf_switch_spdt_2to1";
}

int declaredOutputCount(std::string_view type) {
    for (const auto &entry : rfBudgetStageTypes()) {
        if (entry.type == type)
            return entry.outputs;
    }
    return 1;
}

// Friis and the reciprocal intercept sum for each point. The state that survives from one stage
// to the next carries the infinite no-transmission and the first missing-input reasons.
void fillPoint(std::vector<RfBudgetStageResult> &results, std::size_t point,
               std::optional<double> input_level_dBm, std::uint16_t no_level_reason,
               RfBudgetReasonTable &reasons) {
    double gain_before = 1.0; // product of the gains before the current stage
    double gain_sum_dB = 0.0;
    double f_previous = 1.0; // F_c of the previous stage
    double iip3_inv_mW = 0.0;
    bool gain_missing_before = false;
    std::uint16_t gain_missing_reason = 0;
    bool transmission = false;
    std::uint16_t transmission_reason = 0;
    std::uint16_t iip3_missing_reason = 0;

    for (std::size_t index = 0; index < results.size(); ++index) {
        RfBudgetStageResult &out = results[index];
        const double g = out.gain_dB.values[point];
        const std::uint16_t g_reason = out.gain_dB.reasons[point];
        const double nf = out.nf_dB.values[point];
        const std::uint16_t nf_reason = out.nf_dB.reasons[point];
        const double oip3 = out.oip3_dBm.values[point];
        const std::uint16_t oip3_reason = out.oip3_dBm.reasons[point];

        const bool g_missing = std::isnan(g);
        const bool g_stops = std::isinf(g) && g < 0.0;
        const bool nf_missing = std::isnan(nf);
        const bool oip3_missing = std::isnan(oip3);
        const bool oip3_linear = std::isinf(oip3) && oip3 > 0.0;
        const bool now_transmission = transmission || g_stops;
        const std::uint16_t stop_reason = transmission ? transmission_reason : g_reason;

        // Cumulative gain: the dB sum; a missing gain stops every later sum at NaN; once a stage
        // passes no signal, the sum stays -inf for good.
        double cumulative_gain = kNaN;
        std::uint16_t cumulative_gain_reason = 0;
        if (now_transmission) {
            cumulative_gain = -kInf;
            cumulative_gain_reason = stop_reason;
        } else if (gain_missing_before || g_missing) {
            cumulative_gain = kNaN;
            cumulative_gain_reason = gain_missing_before ? gain_missing_reason : g_reason;
        } else {
            cumulative_gain = gain_sum_dB + g;
        }

        // Cumulative NF (Friis). The current stage's own gain is not an input, so a missing gain
        // here leaves this point's NF intact and only empties later stages.
        double cumulative_nf = kNaN;
        std::uint16_t cumulative_nf_reason = 0;
        double f_through = kNaN;
        if (now_transmission) {
            cumulative_nf = kInf;
            cumulative_nf_reason = stop_reason;
            f_through = kInf;
        } else if (gain_missing_before) {
            cumulative_nf = kNaN;
            cumulative_nf_reason = gain_missing_reason;
        } else if (nf_missing) {
            cumulative_nf = kNaN;
            cumulative_nf_reason = nf_reason;
        } else {
            const double f_stage = std::pow(10.0, nf / 10.0);
            const double f_total =
                index == 0 ? f_stage : f_previous + (f_stage - 1.0) / gain_before;
            if (f_total > 0.0 && !std::isnan(f_total)) {
                f_through = f_total;
                cumulative_nf = 10.0 * std::log10(f_total);
            } else {
                cumulative_nf_reason = nf_reason;
            }
        }

        // Input-referred IIP3: 1/IIP3 = sum of G_through/OIP3 over nonlinear stages. Linear
        // stages contribute nothing and leave the total at +inf with a stated reason.
        double cumulative_iip3 = kNaN;
        std::uint16_t cumulative_iip3_reason = 0;
        if (std::isnan(iip3_inv_mW)) {
            cumulative_iip3_reason = iip3_missing_reason;
        } else if (oip3_missing) {
            iip3_inv_mW = kNaN;
            iip3_missing_reason = oip3_reason;
            cumulative_iip3_reason = iip3_missing_reason;
        } else if (oip3_linear) {
            const std::uint16_t reason =
                reasons.add("No nonlinear stage up to '" + out.stage + "'.");
            cumulative_iip3 = iip3_inv_mW > 0.0 ? 10.0 * std::log10(1.0 / iip3_inv_mW) : kInf;
            cumulative_iip3_reason = iip3_inv_mW > 0.0 ? 0 : reason;
        } else if (!now_transmission && (gain_missing_before || g_missing)) {
            iip3_inv_mW = kNaN;
            iip3_missing_reason = gain_missing_before ? gain_missing_reason : g_reason;
            cumulative_iip3_reason = iip3_missing_reason;
        } else {
            if (!now_transmission) {
                const double gain_through = gain_before * std::pow(10.0, g / 10.0);
                const double oip3_mW = std::pow(10.0, oip3 / 10.0);
                iip3_inv_mW += gain_through / oip3_mW;
            }
            const std::uint16_t reason =
                reasons.add("No nonlinear stage up to '" + out.stage + "'.");
            cumulative_iip3 = iip3_inv_mW > 0.0 ? 10.0 * std::log10(1.0 / iip3_inv_mW) : kInf;
            cumulative_iip3_reason = iip3_inv_mW > 0.0 ? 0 : reason;
        }

        // Signal level at this stage's output.
        double level = kNaN;
        std::uint16_t level_reason = 0;
        if (!input_level_dBm) {
            level_reason = no_level_reason;
        } else if (now_transmission) {
            level = -kInf;
            level_reason = stop_reason;
        } else if (std::isnan(cumulative_gain)) {
            level_reason = cumulative_gain_reason;
        } else {
            level = *input_level_dBm + cumulative_gain;
        }

        // This stage's share of the path noise is filled after the whole path is known: its
        // denominator is the finished total, not the running cumulative NF.
        const double share = kNaN;
        const std::uint16_t share_reason = 0;

        // Headroom to this stage's 1 dB compression point; the law is the engine's own.
        double headroom = kNaN;
        std::uint16_t headroom_reason = 0;
        if (!input_level_dBm) {
            headroom_reason = no_level_reason;
        } else if (now_transmission) {
            headroom = kInf;
            headroom_reason = stop_reason;
        } else if (std::isnan(level)) {
            headroom_reason = level_reason;
        } else if (oip3_missing || oip3_linear) {
            headroom_reason = oip3_reason;
        } else {
            headroom = NonlinearModel::outputLevelForCompression_dBm(oip3, 1.0) - level;
            if (std::isnan(headroom))
                headroom_reason = oip3_reason;
        }

        out.cumulative_gain_dB.values[point] = cumulative_gain;
        out.cumulative_gain_dB.reasons[point] = cumulative_gain_reason;
        out.cumulative_nf_dB.values[point] = cumulative_nf;
        out.cumulative_nf_dB.reasons[point] = cumulative_nf_reason;
        out.cumulative_iip3_dBm.values[point] = cumulative_iip3;
        out.cumulative_iip3_dBm.reasons[point] = cumulative_iip3_reason;
        out.level_dBm.values[point] = level;
        out.level_dBm.reasons[point] = level_reason;
        out.nf_share_percent.values[point] = share;
        out.nf_share_percent.reasons[point] = share_reason;
        out.p1db_headroom_dB.values[point] = headroom;
        out.p1db_headroom_dB.reasons[point] = headroom_reason;

        // Advance the cascade state.
        if (now_transmission) {
            if (!transmission) {
                transmission = true;
                transmission_reason = g_reason;
            }
            gain_before = 0.0;
            gain_sum_dB = -kInf;
            f_previous = kInf;
        } else {
            if (g_missing && !gain_missing_before) {
                gain_missing_before = true;
                gain_missing_reason = g_reason;
            }
            gain_before = gain_missing_before ? kNaN : gain_before * std::pow(10.0, g / 10.0);
            gain_sum_dB = cumulative_gain;
            f_previous = f_through;
        }
    }

    // NF share against the finished path total. A path that adds no noise, a missing total, or a
    // stage that already lost the signal carries a reason instead of a bare NaN.
    const double total_nf_dB = results.back().cumulative_nf_dB.values[point];
    const std::uint16_t total_nf_reason = results.back().cumulative_nf_dB.reasons[point];
    gain_before = 1.0;
    gain_missing_before = false;
    gain_missing_reason = 0;
    transmission = false;
    transmission_reason = 0;
    for (std::size_t index = 0; index < results.size(); ++index) {
        RfBudgetStageResult &out = results[index];
        const double g = out.gain_dB.values[point];
        const std::uint16_t g_reason = out.gain_dB.reasons[point];
        const double nf = out.nf_dB.values[point];
        const std::uint16_t nf_reason = out.nf_dB.reasons[point];
        const bool g_missing = std::isnan(g);
        const bool g_stops = std::isinf(g) && g < 0.0;
        const bool now_transmission = transmission || g_stops;
        const std::uint16_t stop_reason = transmission ? transmission_reason : g_reason;

        double share = kNaN;
        std::uint16_t share_reason = 0;
        if (std::isnan(total_nf_dB) || std::isinf(total_nf_dB)) {
            share_reason = total_nf_reason;
        } else if (total_nf_dB == 0.0) {
            share_reason = reasons.add("The path adds no noise.");
        } else if (now_transmission) {
            share_reason = stop_reason;
        } else if (gain_missing_before) {
            share_reason = gain_missing_reason;
        } else if (std::isnan(nf)) {
            share_reason = nf_reason;
        } else {
            const double f_stage = std::pow(10.0, nf / 10.0);
            const double denominator = std::pow(10.0, total_nf_dB / 10.0) - 1.0;
            const double numerator = (f_stage - 1.0) / gain_before;
            if (denominator > 0.0 && std::isfinite(numerator))
                share = 100.0 * numerator / denominator;
            else
                share_reason = reasons.add("The path adds no noise.");
        }
        out.nf_share_percent.values[point] = share;
        out.nf_share_percent.reasons[point] = share_reason;

        if (now_transmission) {
            if (!transmission) {
                transmission = true;
                transmission_reason = g_reason;
            }
            gain_before = 0.0;
        } else {
            if (g_missing && !gain_missing_before) {
                gain_missing_before = true;
                gain_missing_reason = g_reason;
            }
            gain_before = gain_missing_before ? kNaN : gain_before * std::pow(10.0, g / 10.0);
        }
    }
}

} // namespace

std::vector<RfBudgetStageResult> cascadeRfBudgetPath(const std::vector<RfBudgetPathHop> &hops,
                                                     std::optional<double> input_level_dBm,
                                                     std::string_view mode_name,
                                                     RfBudgetReasonTable &reasons) {
    std::vector<RfBudgetStageResult> results;
    if (hops.empty())
        return results;

    std::size_t point_count = 0;
    for (const auto &hop : hops) {
        if (!hop.response)
            continue;
        if (hop.output_port >= 0 &&
            static_cast<std::size_t>(hop.output_port) < hop.response->gain_dB.size()) {
            point_count = std::max(
                point_count,
                hop.response->gain_dB[static_cast<std::size_t>(hop.output_port)].values.size());
        }
    }

    results.reserve(hops.size());
    for (const auto &hop : hops) {
        RfBudgetStageResult result;
        result.input_port = hop.input_port;
        result.output_port = hop.output_port;
        result.gain_dB = makeSeries(point_count);
        result.nf_dB = makeSeries(point_count);
        result.oip3_dBm = makeSeries(point_count);
        result.cumulative_gain_dB = makeSeries(point_count);
        result.cumulative_nf_dB = makeSeries(point_count);
        result.cumulative_iip3_dBm = makeSeries(point_count);
        result.level_dBm = makeSeries(point_count);
        result.nf_share_percent = makeSeries(point_count);
        result.p1db_headroom_dB = makeSeries(point_count);
        if (hop.response) {
            result.stage = hop.response->stage;
            const std::size_t port = static_cast<std::size_t>(hop.output_port);
            if (hop.output_port >= 0 && port < hop.response->gain_dB.size())
                result.gain_dB = normalized(hop.response->gain_dB[port], point_count);
            if (hop.output_port >= 0 && port < hop.response->nf_dB.size())
                result.nf_dB = normalized(hop.response->nf_dB[port], point_count);
            result.oip3_dBm = normalized(hop.response->oip3_dBm, point_count);
        }
        results.push_back(std::move(result));
    }

    const std::uint16_t no_level_reason =
        input_level_dBm ? 0
                        : reasons.add("Mode '" + std::string(mode_name) + "' has no input level.");
    for (std::size_t point = 0; point < point_count; ++point)
        fillPoint(results, point, input_level_dBm, no_level_reason, reasons);
    return results;
}

void floorRfBudgetReadouts(std::vector<RfBudgetStageResult> &stages, RfBudgetReasonTable &reasons) {
    for (RfBudgetStageResult &out : stages) {
        for (std::size_t point = 0; point < out.cumulative_gain_dB.values.size(); ++point) {
            const double gain = out.cumulative_gain_dB.values[point];
            if (!std::isfinite(gain) || gain >= kGainFloor_dB)
                continue;
            const std::uint16_t reason =
                reasons.add("Output of '" + out.stage + "' is below -100 dB here.");
            // A value that is already blank keeps the reason it has.
            const auto blank = [point, reason](RfBudgetSeries &series) {
                if (!std::isfinite(series.values[point]))
                    return;
                series.values[point] = kNaN;
                series.reasons[point] = reason;
            };
            blank(out.cumulative_gain_dB);
            blank(out.cumulative_nf_dB);
            blank(out.level_dBm);
            blank(out.p1db_headroom_dB);
        }
    }
}

namespace {

struct RouteState {
    const RfBudget &budget;
    const std::map<std::string, std::map<std::string, double>> &overrides;
    RfBudgetStageEvaluator &evaluator;
    const std::vector<double> &grid;
    const ReceiverRequirementsConfig &config;
    std::optional<double> input_level_dBm;
    std::string_view mode_name;
    RfBudgetReasonTable &reasons;
    std::map<std::pair<std::size_t, int>, RfBudgetStageResponse> responses;
    std::vector<char> done;
    std::vector<std::uint16_t> blocked_reason; // first blocked reason per output; 0 if none
    std::vector<RfBudgetCascade> cascades;
    std::vector<RfBudgetUnreachedOutput> unreached;
};

// Output SNR in dB at each point: the input level less kTB at the config's noise bandwidth, less
// the cumulative NF. Gain cancels, since the signal and the output noise both carry it. Without an
// input level the series is NaN; a path that passes no signal (infinite NF) gives -inf.
std::vector<double> outputSnrSeries(const RfBudgetStageResult &last,
                                    const ReceiverRequirementsConfig &config) {
    std::vector<double> snr(last.cumulative_nf_dB.values.size(), kNaN);
    const auto input_level = config.measurement_conditions.input_level_dBm;
    if (!config.snr || !input_level)
        return snr;
    const double kTB_dBm =
        10.0 * std::log10(k * T) + 30.0 + 10.0 * std::log10(config.snr->bandwidth_Hz);
    for (std::size_t i = 0; i < snr.size(); ++i)
        snr[i] = *input_level - kTB_dBm - last.cumulative_nf_dB.values[i];
    return snr;
}

void finalizeVerdict(RfBudgetCascade &cascade, const ReceiverRequirementsConfig &config,
                     RfBudgetReasonTable &reasons) {
    if (cascade.stages.empty() || cascade.frequencies_Hz.empty())
        return;
    RfBudgetStageResult &last = cascade.stages.back();
    std::vector<double> iip3 = last.cumulative_iip3_dBm.values;
    bool any_infinite = false;
    for (const double value : iip3) {
        if (std::isinf(value) && value > 0.0) {
            any_infinite = true;
            break;
        }
    }
    if (any_infinite) {
        const std::uint16_t reason =
            reasons.add("No nonlinear stage on the path; IIP3 cannot be measured.");
        for (std::size_t i = 0; i < iip3.size(); ++i) {
            if (std::isinf(iip3[i]) && iip3[i] > 0.0) {
                iip3[i] = kNaN;
                last.cumulative_iip3_dBm.values[i] = kNaN;
                last.cumulative_iip3_dBm.reasons[i] = reason;
            }
        }
    }

    ReceiverRequirementsState state;
    state.config = config;
    cascade.verdict = evaluateReceiverRequirements(
        state, cascade.frequencies_Hz.front(), cascade.frequencies_Hz.back(),
        cascade.frequencies_Hz, last.cumulative_gain_dB.values, last.cumulative_nf_dB.values,
        last.level_dBm.values, iip3, true, outputSnrSeries(last, config));
}

void walk(RouteState &state, std::size_t stage_index, int input_port,
          const std::vector<RfBudgetPathHop> &hops, std::uint16_t blocked_reason,
          std::vector<char> &on_path) {
    const RfBudgetStage &stage = state.budget.stages[stage_index];
    const int declared_outputs = declaredOutputCount(stage.type);

    if (blocked_reason != 0) {
        // The signal never reaches this stage: keep finding the outputs downstream so each one
        // reports the switch that blocked it. No engine is built on a dead branch.
        for (int port = 0; port < declared_outputs; ++port) {
            for (std::size_t o = 0; o < state.budget.outputs.size(); ++o) {
                const RfBudgetOutput &output = state.budget.outputs[o];
                if (state.done[o] || output.stage != stage.name || output.port != port)
                    continue;
                // A blocked route does not claim the output: a live route may reach it later.
                if (state.blocked_reason[o] == 0)
                    state.blocked_reason[o] = blocked_reason;
            }
            for (std::size_t next = 0; next < state.budget.stages.size(); ++next) {
                if (on_path[next])
                    continue;
                const RfBudgetStage &candidate = state.budget.stages[next];
                for (std::size_t port_index = 0; port_index < candidate.inputs.size();
                     ++port_index) {
                    const auto &feed = candidate.inputs[port_index];
                    if (!feed || !feed->stage || *feed->stage != stage.name || feed->port != port)
                        continue;
                    on_path[next] = 1;
                    walk(state, next, static_cast<int>(port_index), hops, blocked_reason, on_path);
                    on_path[next] = 0;
                }
            }
        }
        return;
    }

    const std::map<std::string, double> *stage_overrides = nullptr;
    const auto override_entry = state.overrides.find(stage.name);
    if (override_entry != state.overrides.end())
        stage_overrides = &override_entry->second;
    RfBudgetStageResponse evaluated =
        state.evaluator.evaluate(stage, stage_overrides, state.grid, input_port);
    const auto key = std::make_pair(stage_index, input_port);
    RfBudgetStageResponse &response =
        state.responses.emplace(key, std::move(evaluated)).first->second;

    const int response_outputs =
        std::min<int>(declared_outputs, static_cast<int>(response.gain_dB.size()));
    std::vector<int> selected;
    std::uint16_t dead_reason = 0;
    if (response.stage_reason != 0) {
        dead_reason = isSwitchType(stage.type)
                          ? state.reasons.add("Switch '" + stage.name + "' could not be evaluated.")
                          : response.stage_reason;
    } else if (isSwitchType(stage.type)) {
        const bool throw_known = response.active_throw >= 0 && response.active_throw < 2;
        if (!throw_known ||
            (stage.type == "rf_switch_spdt" && response.active_throw >= response_outputs)) {
            dead_reason = state.reasons.add("Switch '" + stage.name + "' could not be evaluated.");
        } else {
            dead_reason =
                state.reasons.add("Switch '" + stage.name + "' selects T" +
                                  std::to_string(response.active_throw + 1) + " in this mode.");
            if (stage.type == "rf_switch_spdt")
                selected.push_back(response.active_throw);
            else if (input_port == response.active_throw)
                selected.push_back(0);
        }
    } else if (stage.type == "splitter") {
        for (int port = 0; port < response_outputs; ++port)
            selected.push_back(port);
    } else if (response_outputs > 0) {
        selected.push_back(0);
    }

    // Reached ports come first: a blocked branch may share a downstream output with a reached
    // one, and the reached cascade must claim it before the blocked branch reports it unreached.
    for (int phase = 0; phase < 2; ++phase) {
        for (int port = 0; port < declared_outputs; ++port) {
            const bool reached =
                response.stage_reason == 0 &&
                std::find(selected.begin(), selected.end(), port) != selected.end();
            const std::uint16_t child_blocked = reached ? 0 : dead_reason;
            if ((phase == 0) != (child_blocked == 0))
                continue;

            std::vector<RfBudgetPathHop> child_hops = hops;
            if (reached)
                child_hops.push_back({&response, input_port, port});

            for (std::size_t o = 0; o < state.budget.outputs.size(); ++o) {
                const RfBudgetOutput &output = state.budget.outputs[o];
                if (state.done[o] || output.stage != stage.name || output.port != port)
                    continue;
                if (child_blocked != 0) {
                    // A blocked route does not claim the output: a live route may reach it later.
                    if (state.blocked_reason[o] == 0)
                        state.blocked_reason[o] = child_blocked;
                    continue;
                }
                state.done[o] = true;
                RfBudgetCascade cascade;
                cascade.output = output.name;
                cascade.frequencies_Hz = state.grid;
                cascade.stages = cascadeRfBudgetPath(child_hops, state.input_level_dBm,
                                                     state.mode_name, state.reasons);
                finalizeVerdict(cascade, state.config, state.reasons);
                floorRfBudgetReadouts(cascade.stages, state.reasons);
                state.cascades.push_back(std::move(cascade));
            }

            for (std::size_t next = 0; next < state.budget.stages.size(); ++next) {
                if (on_path[next])
                    continue;
                const RfBudgetStage &candidate = state.budget.stages[next];
                for (std::size_t port_index = 0; port_index < candidate.inputs.size();
                     ++port_index) {
                    const auto &feed = candidate.inputs[port_index];
                    if (!feed || !feed->stage || *feed->stage != stage.name || feed->port != port)
                        continue;
                    on_path[next] = 1;
                    walk(state, next, static_cast<int>(port_index), child_hops, child_blocked,
                         on_path);
                    on_path[next] = 0;
                }
            }
        }
    }
}

} // namespace

RfBudgetEvaluation evaluateRfBudget(const RfBudget &budget,
                                    const ReceiverRequirementsSet &requirements,
                                    const ComponentLibrary &library,
                                    std::optional<std::uint32_t> only_mode_id) {
    RfBudgetEvaluation evaluation;
    if (!budget.invalid_reason.empty()) {
        evaluation.not_evaluated_reason = "The RF budget is invalid: " + budget.invalid_reason;
        return evaluation;
    }
    if (!requirements.invalid_reason.empty()) {
        evaluation.not_evaluated_reason =
            "The receiver requirements are invalid: " + requirements.invalid_reason;
        return evaluation;
    }
    if (budget.stages.empty()) {
        evaluation.not_evaluated_reason = "The budget has no stages.";
        return evaluation;
    }
    if (budget.outputs.empty()) {
        evaluation.not_evaluated_reason = "The budget has no outputs.";
        return evaluation;
    }

    std::vector<std::uint32_t> mode_ids;
    mode_ids.reserve(requirements.modes.size());
    for (const auto &mode : requirements.modes)
        mode_ids.push_back(mode.id);
    for (const auto &record : budget.modes) {
        if (std::find(mode_ids.begin(), mode_ids.end(), record.mode_id) == mode_ids.end())
            evaluation.orphaned_mode_ids.push_back(record.mode_id);
    }

    RfBudgetStageEvaluator evaluator(library, evaluation.reasons);
    for (const auto &mode : requirements.modes) {
        if (only_mode_id && mode.id != *only_mode_id)
            continue;
        RfBudgetModeResult mode_result;
        mode_result.mode_id = mode.id;
        mode_result.mode_name = mode.name;
        if (!mode.config) {
            mode_result.status = RfBudgetModeStatus::NotConfigured;
            evaluation.modes.push_back(std::move(mode_result));
            continue;
        }
        const RfBudgetModeRecord *record = findRfBudgetModeRecord(budget, mode.id);
        if (!record) {
            mode_result.status = RfBudgetModeStatus::NotBudgeted;
            evaluation.modes.push_back(std::move(mode_result));
            continue;
        }
        mode_result.status = RfBudgetModeStatus::Budgeted;

        const std::vector<double> grid = [&] {
            std::vector<double> points = linearSweepFrequencies(
                mode.config->band_start_Hz, mode.config->band_stop_Hz, budget.point_count);
            for (const auto &point : mode.config->oob_suppression)
                points.push_back(receiverOOBSuppressionFrequency_Hz(*mode.config, point));
            std::sort(points.begin(), points.end());
            points.erase(std::unique(points.begin(), points.end()), points.end());
            return points;
        }();
        RouteState state{budget,
                         record->overrides,
                         evaluator,
                         grid,
                         *mode.config,
                         mode.config->measurement_conditions.input_level_dBm,
                         mode.name,
                         evaluation.reasons,
                         {},
                         std::vector<char>(budget.outputs.size(), 0),
                         std::vector<std::uint16_t>(budget.outputs.size(), 0),
                         {},
                         {}};

        std::vector<char> on_path(budget.stages.size(), 0);
        for (std::size_t index = 0; index < budget.stages.size(); ++index) {
            const RfBudgetStage &stage = budget.stages[index];
            for (std::size_t port = 0; port < stage.inputs.size(); ++port) {
                const auto &feed = stage.inputs[port];
                if (!feed || feed->stage)
                    continue;
                const std::vector<RfBudgetPathHop> hops;
                on_path[index] = 1;
                walk(state, index, static_cast<int>(port), hops, 0, on_path);
                on_path[index] = 0;
            }
        }
        for (std::size_t o = 0; o < budget.outputs.size(); ++o) {
            if (state.done[o])
                continue;
            const std::uint16_t blocked = state.blocked_reason[o];
            if (blocked != 0) {
                state.unreached.push_back({budget.outputs[o].name, state.reasons.at(blocked)});
                continue;
            }
            state.unreached.push_back({budget.outputs[o].name,
                                       "Output '" + budget.outputs[o].name + "' is not reached."});
        }

        mode_result.cascades = std::move(state.cascades);
        mode_result.unreached = std::move(state.unreached);
        evaluation.modes.push_back(std::move(mode_result));
    }
    return evaluation;
}

const RfBudgetModeResult *findRfBudgetModeResult(const RfBudgetEvaluation &evaluation,
                                                 std::uint32_t mode_id) {
    for (const auto &mode : evaluation.modes) {
        if (mode.mode_id == mode_id)
            return &mode;
    }
    return nullptr;
}
