#include "budget_stage_evaluator.h"

#include "amplifier_engine.h"
#include "circuit_runtime.h"
#include "common.h"
#include "component_params.h"
#include "graph_editor_actions.h"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The frequency-matching tolerance NetworkAnalyzerEngine::computeMeasurement() uses for the
// output tone and the noise bin.
constexpr double kFreqEpsilonHz = 1.0;

const double kInfinity = std::numeric_limits<double>::infinity();

// A stage type with no OIP3/P1dB model contributes nothing to those cascades, so the default
// is +infinity. Points that cannot be evaluated are reset to NaN below.
BudgetStageMetrics emptyMetrics(std::size_t points) {
    BudgetStageMetrics metrics;
    metrics.gain_dB.assign(points, kNaN);
    metrics.nf_dB.assign(points, kNaN);
    metrics.oip3_dBm.assign(points, kInfinity);
    metrics.p1db_dBm.assign(points, kInfinity);
    metrics.out_of_band.assign(points, false);
    return metrics;
}

// An unresolved stage has no usable values anywhere; NaN intercepts keep it from reading as a
// passive with no OIP3/P1dB model.
BudgetStageEvaluation unresolvedEvaluation(std::size_t points, std::string diagnostic) {
    BudgetStageEvaluation evaluation;
    evaluation.metrics = emptyMetrics(points);
    evaluation.metrics.oip3_dBm.assign(points, kNaN);
    evaluation.metrics.p1db_dBm.assign(points, kNaN);
    evaluation.resolved = false;
    evaluation.diagnostic = std::move(diagnostic);
    return evaluation;
}

// Index of the first tone within 1 Hz of `f`, as the analyzer's gain read does.
std::optional<std::size_t> findTone(const Spectrum &spectrum, double f) {
    for (std::size_t i = 0; i < spectrum.tones.size(); ++i) {
        if (std::abs(spectrum.tones[i].freq_Hz - f) <= kFreqEpsilonHz) {
            return i;
        }
    }
    return std::nullopt;
}

// Index of the first frequency bin within 1 Hz of `f`, as the analyzer's noise read does.
std::optional<std::size_t> findNoiseBin(const Spectrum &spectrum, double f) {
    for (std::size_t i = 0; i < spectrum.frequencies.size(); ++i) {
        if (std::abs(spectrum.frequencies[i] - f) <= kFreqEpsilonHz) {
            return i;
        }
    }
    return std::nullopt;
}

// The engines zero their noise path on a one-bin grid (`N < 2`), so the stimulus carries two
// bins at the same frequency: the tone's, and one only to keep the noise path active.
Spectrum makeStimulus(double f, double stimulus_power_dBm) {
    Spectrum stimulus;
    stimulus.frequencies = {f, f};
    stimulus.tones = {{f, stimulus_power_dBm, 0.0}};
    stimulus.noise_W = {k * T, k * T};
    stimulus.noise_added_W = {0.0, 0.0};
    stimulus.phase_deg = {0.0, 0.0};
    stimulus.computeTotalNoise();
    stimulus.bumpGeneration();
    return stimulus;
}

// Resolves every data-file reference against the library JSON's directory before
// instantiate(), so a reference that cannot even be located is reported before any engine
// exists. A file that exists but does not load is reported by instantiate() instead.
std::optional<std::string> checkDataFiles(const ComponentDefinition &definition) {
    std::set<std::string> checked;
    for (const auto &ref : definition.data_files) {
        // componentDataFilePath() returns the first entry of a type, so later duplicates of
        // the same type cannot resolve to a different file.
        if (!checked.insert(ref.type).second) {
            continue;
        }
        const auto path = componentDataFilePath(definition, ref.type);
        if (!path) {
            return "cannot resolve data file '" + ref.path + "' against the library directory";
        }
        std::error_code ec;
        if (!std::filesystem::exists(*path, ec) || ec) {
            return "data file not found: " + path->string();
        }
    }
    return std::nullopt;
}

// "s_parameters 'bad.s2p': failed to load the S-parameter file (or it did not enable
// S-parameter mode); ..." — one clause per unusable data file or path parameter.
std::string describeDataFileIssues(const std::vector<ComponentLibrary::DataFileLoadIssue> &issues) {
    std::ostringstream text;
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i != 0) {
            text << "; ";
        }
        text << issues[i].type << " '" << issues[i].path << "': " << issues[i].reason;
    }
    return text.str();
}

// True only when the stage actually names a file: an absent key, a non-string value, and
// an empty string all declare no file. instantiate() treats "" the same way.
bool stageDeclaresParameter(const BudgetStage &stage, const char *key) {
    return stage.parameters.is_object() && stage.parameters.contains(key) &&
           stage.parameters[key].is_string() && !stage.parameters[key].get<std::string>().empty();
}

// Whether the stage names a file at all: data_files, or any path parameter with a non-empty
// value. Only such a stage needs its source path confined to a scanned library root.
bool declaresFileInput(const BudgetStage &stage) {
    if (!stage.data_files.empty()) {
        return true;
    }
    for (const char *key : kPathParameterKeys) {
        if (stageDeclaresParameter(stage, key)) {
            return true;
        }
    }
    return false;
}

ComponentDefinition definitionFor(const BudgetStage &stage) {
    ComponentDefinition definition;
    definition.schema_version = 1;
    definition.type = stage.type;
    definition.part_number = stage.part_number;
    definition.manufacturer = stage.manufacturer;
    definition.description = stage.description;
    definition.parameters = stage.parameters;
    definition.data_files = stage.data_files;
    definition.source_path = stage.source_path;
    return definition;
}

std::string cacheKey(const BudgetConfig &config, const std::vector<double> &frequencies_Hz,
                     double stimulus_power_dBm) {
    std::ostringstream key;
    key << std::setprecision(17);
    key << budgetToJson(config).dump() << '|' << stimulus_power_dBm << '|';
    for (const double f : frequencies_Hz) {
        key << f << ',';
    }
    return key.str();
}

} // namespace

std::vector<BudgetStageEvaluation>
BudgetStageEvaluator::evaluate(const BudgetConfig &config, ComponentLibrary &library,
                               const std::vector<double> &frequencies_Hz,
                               double stimulus_power_dBm) {
    const std::string key = cacheKey(config, frequencies_Hz, stimulus_power_dBm);
    if (key == m_cache_key) {
        return m_cache;
    }

    const std::size_t points = frequencies_Hz.size();
    std::vector<BudgetStageEvaluation> evaluations;
    evaluations.reserve(config.stages.size());

    for (const BudgetStage &stage : config.stages) {
        const ComponentDefinition definition = definitionFor(stage);
        // A reference can only be resolved against a directory the library actually indexed;
        // otherwise checkDataFiles() reports the file as missing, which misattributes the
        // problem to the file instead of to the stage's unscanned source library.
        if (declaresFileInput(stage) && !library.containsLibraryPath(stage.source_path)) {
            evaluations.push_back(unresolvedEvaluation(
                points, "stage source path is outside the scanned library roots"));
            continue;
        }
        if (const auto data_file_error = checkDataFiles(definition)) {
            evaluations.push_back(unresolvedEvaluation(points, *data_file_error));
            continue;
        }

        CircuitRuntime runtime;
        GraphEditorActions actions(runtime);
        std::vector<ComponentLibrary::DataFileLoadIssue> data_file_issues;
        IComponentEngine *engine =
            library.instantiate(definition, runtime, actions, std::nullopt, &data_file_issues);
        if (!engine) {
            evaluations.push_back(unresolvedEvaluation(
                points, "cannot instantiate '" + stage.part_number + "' (" + stage.type + ")"));
            continue;
        }
        // instantiate() still returns an engine that fell back to the definition's scalar
        // defaults. A budget stage must not be evaluated from a substituted response.
        if (!data_file_issues.empty()) {
            evaluations.push_back(
                unresolvedEvaluation(points, describeDataFileIssues(data_file_issues)));
            continue;
        }
        // A stage that names a data file in its parameters must have loaded it. The path may
        // resolve and exist while the engine still fell back to its scalar model (a parse
        // failure or a file that does not enable the mode), which the issue list above does
        // not see: only the engine state can show it.
        const auto declaredValue = [&stage](const char *key) {
            return stageDeclaresParameter(stage, key) ? stage.parameters[key].get<std::string>()
                                                      : std::string{};
        };
        const std::string sparam_file = declaredValue("sparam_filepath");
        const std::string sparam_legacy_file = declaredValue("sparam_path");
        if ((!sparam_file.empty() || !sparam_legacy_file.empty()) && !engine->sparamMode()) {
            const std::string &file = sparam_file.empty() ? sparam_legacy_file : sparam_file;
            evaluations.push_back(unresolvedEvaluation(
                points, "'" + stage.part_number + "' declares an S-parameter file '" + file +
                            "' that did not load and did not enable S-parameter mode"));
            continue;
        }
        if (const auto *amplifier = dynamic_cast<const AmplifierEngine *>(engine)) {
            // A selected amp-f block replaces the AMP file's intercepts, so a broken legacy
            // file is only a defect when no block replaces it.
            const std::string amp_file = declaredValue("amp_filepath");
            if (!amp_file.empty() && !amplifier->ampfBlockSelected() && !amplifier->ampLoaded()) {
                std::string diagnostic = "'" + stage.part_number + "' declares an AMP file '" +
                                         amp_file + "' that did not load";
                if (!amplifier->ampError().empty()) {
                    diagnostic += ": " + amplifier->ampError();
                }
                evaluations.push_back(unresolvedEvaluation(points, std::move(diagnostic)));
                continue;
            }
            const std::string ampf_file = declaredValue("ampf_filepath");
            if (!ampf_file.empty() && !amplifier->ampfBlockSelected()) {
                std::string diagnostic = "'" + stage.part_number + "' declares an amp-f file '" +
                                         ampf_file + "' without a selected block";
                if (!amplifier->ampfError().empty()) {
                    diagnostic += ": " + amplifier->ampfError();
                }
                evaluations.push_back(unresolvedEvaluation(points, std::move(diagnostic)));
                continue;
            }
        }
        // A budget stage is a two-port with exactly one input and one output: the probe
        // drives inputs[0] and reads outputs[0]. A source (zero inputs), a combiner (two
        // inputs), or a splitter (two outputs) must not reach that code.
        if (engine->node().inputs.size() != 1 || engine->node().outputs.size() != 1) {
            evaluations.push_back(unresolvedEvaluation(
                points, "'" + stage.part_number + "' (" + stage.type +
                            ") is not a single-input, single-output two-port and cannot be "
                            "evaluated"));
            continue;
        }

        BudgetStageEvaluation evaluation;
        evaluation.metrics = emptyMetrics(points);

        // A budget stage is a linear two-port: its nonlinearity is represented by the
        // OIP3/P1dB columns, not by compressing the probe tone. Library definitions omit
        // `enable_nonlinear`, which makes the amplifier's deserialize() enable its nonlinear
        // model; turn it off so the gain/NF probe stays small-signal.
        auto *amplifier = dynamic_cast<AmplifierEngine *>(engine);
        if (amplifier) {
            amplifier->setEnableNonlinear(false);
        }

        for (std::size_t i = 0; i < points; ++i) {
            const double f = frequencies_Hz[i];
            Spectrum stimulus = makeStimulus(f, stimulus_power_dBm);
            engine->node().inputs[0] = &stimulus;
            engine->update(0.0);

            const Spectrum &response = engine->node().outputs[0];
            const auto tone_idx = findTone(response, f);
            if (!tone_idx) {
                continue; // no output at this frequency: leave the point invalid
            }
            const auto noise_idx = findNoiseBin(response, f);

            // A tone or bin an S-parameter block flagged out of band holds the nearest edge
            // value, not a measurement. The whole point is out of band and no edge-held
            // intercept may enter the cascade.
            const bool noise_out_of_band = noise_idx && *noise_idx < response.out_of_band.size() &&
                                           response.out_of_band[*noise_idx];
            if (response.tones[*tone_idx].out_of_band || noise_out_of_band) {
                evaluation.metrics.out_of_band[i] = true;
                evaluation.metrics.gain_dB[i] = kNaN;
                evaluation.metrics.nf_dB[i] = kNaN;
                evaluation.metrics.oip3_dBm[i] = kNaN;
                evaluation.metrics.p1db_dBm[i] = kNaN;
                continue;
            }

            const double gain_dB = response.tones[*tone_idx].power_dBm - stimulus_power_dBm;
            if (!std::isfinite(gain_dB)) {
                continue;
            }
            evaluation.metrics.gain_dB[i] = gain_dB;

            if (noise_idx && *noise_idx < response.noise_total_W.size()) {
                const double noise_out_W = response.noise_total_W[*noise_idx];
                const double gain_linear = dbToLinear(gain_dB);
                if (gain_linear > 0.0 && std::isfinite(noise_out_W)) {
                    const double nf_linear = (noise_out_W / gain_linear) / (k * T);
                    if (nf_linear > 0.0) {
                        evaluation.metrics.nf_dB[i] = 10.0 * std::log10(nf_linear);
                    }
                }
            }

            // An amplifier contributes its OIP3/P1dB spec whether or not its simulation
            // nonlinearity is enabled. Only stage types without an OIP3/P1dB model (passives)
            // stay +infinity.
            if (amplifier) {
                evaluation.metrics.oip3_dBm[i] = amplifier->oip3At_Hz(f);
                evaluation.metrics.p1db_dBm[i] = amplifier->p1dbAt_Hz(f);
            }
        }

        evaluations.push_back(std::move(evaluation));
    }

    m_cache_key = key;
    m_cache = std::move(evaluations);
    return m_cache;
}

void BudgetStageEvaluator::clearCache() {
    m_cache_key.clear();
    m_cache.clear();
}
