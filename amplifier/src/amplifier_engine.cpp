#include "amplifier_engine.h"
#include "logging_core.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

AmplifierEngine::AmplifierEngine(int id, NodeGraphEngine &graph)
    : ComponentEngineBase(id, graph, "Amplifier", 1, 1) {}

void AmplifierEngine::setSParamFilepath(const std::string &path) {
    m_sparam_filepath = path;
    m_sparam_mode = m_sparam_data.load(path) && m_sparam_data.numPorts() >= 2;
    if (m_sparam_data.loaded())
        m_sparam_fwd_idx = 1 * m_sparam_data.numPorts() + 0; // S21
    m_dirty = true;
}

namespace {

// Keeps a derived intercept inside the range the AMP parser accepts. NaN maps to the
// floor, so a bad gain never reaches the nonlinear model as a NaN coefficient.
double clampIntercept(double dBm) {
    if (!(dBm >= AmpFileParser::kMinInterceptDbm))
        return AmpFileParser::kMinInterceptDbm;
    return std::min(dBm, AmpFileParser::kMaxInterceptDbm);
}

// Clamps a finite amp-f intercept to the range the scalar model accepts. Callers reject NaN
// first, so a missing value never reaches this function.
double clampFinite(double dBm) {
    return std::min(std::max(dBm, AmpFileParser::kMinInterceptDbm),
                    AmpFileParser::kMaxInterceptDbm);
}

// Gain in dB at freq_Hz from the amplitude gain function. A zero gain gives -inf, which the
// callers reject as missing.
double gainDb(const NonlinearModel::GainFn &gain, double freq_Hz) {
    return 20.0 * std::log10(gain(freq_Hz));
}

// An amp-f selection as JSON: each VAR or COND key with its value.
nlohmann::json selectionToJson(const AmpfValues &values) {
    nlohmann::json json = nlohmann::json::object();
    for (const auto &[key, value] : values)
        json[key] = value;
    return json;
}

// The selection read from a project file. Entries that are not numbers are ignored.
AmpfValues selectionFromJson(const nlohmann::json &json) {
    AmpfValues values;
    if (!json.is_object())
        return values;
    for (const auto &item : json.items()) {
        if (item.value().is_number())
            values[item.key()] = item.value().get<double>();
    }
    return values;
}

} // namespace

void AmplifierEngine::setAmpFilepath(const std::string &path) {
    loadAmpFile(path);
    m_dirty = true;
}

void AmplifierEngine::loadAmpFile(const std::string &path) {
    m_amp_filepath = path;
    m_amp.reset();
    m_amp_error.clear();
    if (path.empty())
        return;
    AmpParseResult parsed = AmpFileParser::parse(path);
    if (parsed.file) {
        m_amp = std::move(parsed.file);
        LOG_INFO("Loaded AMP intercepts from %s", path.c_str());
    } else {
        m_amp_error = std::move(parsed.error);
        LOG_WARN("AMP file rejected; scalar OIP3 applies: %s", m_amp_error.c_str());
    }
}

// Third-order intercept at each frequency. An OIP3 section is used as given, and it wins
// when the file also has an IIP3 section. An IIP3 section alone gives OIP3 = IIP3 plus the
// small-signal gain at the same frequency.
NonlinearModel::Oip3Fn
AmplifierEngine::intercept3Function(const NonlinearModel::GainFn &gain) const {
    if (ampfBlockSelected())
        return ampfOip3Function(gain);
    if (!m_amp)
        return {};
    if (m_amp->oip3) {
        const AmpIp3Curve *curve = &*m_amp->oip3;
        return [curve](double freq_Hz) { return clampIntercept(curve->dBmAt(freq_Hz)); };
    }
    if (m_amp->iip3) {
        const AmpIp3Curve *curve = &*m_amp->iip3;
        return [curve, gain](double freq_Hz) {
            return clampIntercept(curve->dBmAt(freq_Hz) + 20.0 * std::log10(gain(freq_Hz)));
        };
    }
    return {};
}

void AmplifierEngine::setAmpfFilepath(const std::string &path) {
    loadAmpfFile(path);
    m_dirty = true;
}

void AmplifierEngine::setAmpfSelection(const AmpfValues &wanted) {
    m_ampf_selection = wanted;
    resolveAmpfBlock();
    m_dirty = true;
}

void AmplifierEngine::loadAmpfFile(const std::string &path) {
    m_ampf_filepath = path;
    m_ampf.reset();
    m_ampf_error.clear();
    m_ampf_block = -1;
    if (path.empty())
        return;
    AmpfParseResult parsed = AmpfParser::parse(path);
    if (!parsed.file) {
        m_ampf_error = std::move(parsed.error);
        LOG_WARN("amp-f file rejected; its intercepts are not used: %s", m_ampf_error.c_str());
        return;
    }
    m_ampf = std::move(parsed.file);
    LOG_INFO("Loaded amp-f file %s", path.c_str());
    resolveAmpfBlock();
}

// Picks the block that matches the selection. A selection that matches no block, or more than
// one, leaves no block active, so the amp-f intercepts are not used.
void AmplifierEngine::resolveAmpfBlock() {
    m_ampf_block = -1;
    if (!m_ampf)
        return;
    const AmpfSelection selection = selectAmpfBlock(*m_ampf, m_ampf_selection);
    if (selection.match == AmpfMatch::One) {
        m_ampf_block = static_cast<int>(selection.block - m_ampf->blocks.data());
        m_ampf_error.clear();
        return;
    }
    m_ampf_error = selection.match == AmpfMatch::None
                       ? "no amp-f block has the selected VAR and COND values"
                       : "more than one amp-f block matches the selection";
    LOG_WARN("amp-f block not selected; its intercepts are not used: %s", m_ampf_error.c_str());
}

// Third-order intercept at each frequency from the selected block. The first finite source
// wins: OIP3; IIP3 plus the gain; OP1dB plus the seeding offset of the P1dB rule; IP1dB plus
// the gain plus that offset. The scalar OIP3 applies when none is finite.
NonlinearModel::Oip3Fn AmplifierEngine::ampfOip3Function(const NonlinearModel::GainFn &gain) const {
    const AmpfBlock *block = activeAmpfBlock();
    const int oip3 = m_ampf->columnIndex("OIP3_dBm");
    const int iip3 = m_ampf->columnIndex("IIP3_dBm");
    const int op1db = m_ampf->columnIndex("OP1dB_dBm");
    const int ip1db = m_ampf->columnIndex("IP1dB_dBm");
    const double scalar = m_nonlinear.oip3_dBm();
    return [block, gain, oip3, iip3, op1db, ip1db, scalar](double freq_Hz) {
        if (oip3 >= 0) {
            const double value = block->valueAt(static_cast<size_t>(oip3), freq_Hz);
            if (std::isfinite(value))
                return clampFinite(value);
        }
        if (iip3 >= 0) {
            const double value =
                block->valueAt(static_cast<size_t>(iip3), freq_Hz) + gainDb(gain, freq_Hz);
            if (std::isfinite(value))
                return clampFinite(value);
        }
        if (op1db >= 0) {
            const double value = block->valueAt(static_cast<size_t>(op1db), freq_Hz) +
                                 NonlinearModel::kOip3OverP1dB_dB;
            if (std::isfinite(value))
                return clampFinite(value);
        }
        if (ip1db >= 0) {
            const double value = block->valueAt(static_cast<size_t>(ip1db), freq_Hz) +
                                 gainDb(gain, freq_Hz) + NonlinearModel::kOip3OverP1dB_dB;
            if (std::isfinite(value))
                return clampFinite(value);
        }
        return scalar;
    };
}

// Second-order intercept at each frequency from the selected block. The first finite source
// wins: OIP2; IIP2 plus the gain; the scalar OIP2.
NonlinearModel::Oip2Fn
AmplifierEngine::intercept2Function(const NonlinearModel::GainFn &gain) const {
    return ampfBlockSelected() ? ampfOip2Function(gain) : NonlinearModel::Oip2Fn{};
}

NonlinearModel::Oip2Fn AmplifierEngine::ampfOip2Function(const NonlinearModel::GainFn &gain) const {
    const AmpfBlock *block = activeAmpfBlock();
    const int oip2 = m_ampf->columnIndex("OIP2_dBm");
    const int iip2 = m_ampf->columnIndex("IIP2_dBm");
    const double scalar = m_nonlinear.oip2_dBm();
    return [block, gain, oip2, iip2, scalar](double freq_Hz) {
        if (oip2 >= 0) {
            const double value = block->valueAt(static_cast<size_t>(oip2), freq_Hz);
            if (std::isfinite(value))
                return clampFinite(value);
        }
        if (iip2 >= 0) {
            const double value =
                block->valueAt(static_cast<size_t>(iip2), freq_Hz) + gainDb(gain, freq_Hz);
            if (std::isfinite(value))
                return clampFinite(value);
        }
        return scalar;
    };
}

void AmplifierEngine::update(double dt) {
    (void)dt;
    const Spectrum *in_ptr = m_node.inputs.empty() ? nullptr : m_node.inputs[0];
    if (!m_dirty && in_ptr == m_cached_input_ptr &&
        (!in_ptr || in_ptr->generation == m_cached_input_generation))
        return;

    // --- S-parameter mode ---
    if (m_sparam_mode && m_sparam_data.loaded()) {
        if (!m_dirty && in_ptr == m_cached_sparam_input &&
            (!in_ptr || in_ptr->generation == m_cached_sparam_generation))
            return;
        m_dirty = false;
        m_cached_sparam_input = in_ptr;
        m_cached_input_ptr = in_ptr;
        if (in_ptr) {
            m_cached_sparam_generation = in_ptr->generation;
            m_cached_input_generation = in_ptr->generation;
        }

        auto &out = m_node.outputs[0];

        if (in_ptr && !in_ptr->frequencies.empty())
            out.frequencies = in_ptr->frequencies;
        else if (out.frequencies.size() < 2)
            buildDefaultFrequencyGrid(out.frequencies);

        out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
        out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
        const size_t N = out.frequencies.size();

        // Apply S21 complex gain to tones
        for (auto &t : out.tones) {
            auto S = m_sparam_data.interpolate(t.freq_Hz, m_sparam_fwd_idx);
            double mag = std::abs(S);
            t.power_dBm += 20.0 * std::log10(mag);
            t.phase_deg += std::arg(S) * 180.0 / std::numbers::pi;
        }
        m_sparam_data.markOutOfBand(in_ptr, out);

        // Nonlinear processing
        if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
            size_t n_fund = out.tones.size();
            const NonlinearModel::GainFn gain = [this](double freq) {
                auto S = this->m_sparam_data.interpolate(freq, this->m_sparam_fwd_idx);
                return std::abs(S);
            };
            auto result = m_nonlinear.process(in_ptr->tones, gain, intercept3Function(gain),
                                              intercept2Function(gain));
            for (const auto &t : result.extra_tones)
                out.tones.push_back(t);
            for (size_t i = 0; i < n_fund; ++i)
                out.tones[i].power_dBm += result.tone_compression_dB[i];
        }

        if (in_ptr && !in_ptr->phase_deg.empty())
            out.phase_deg = in_ptr->phase_deg;
        else
            out.phase_deg.assign(N, 0.0);

        if (N < 2) {
            out.noise_W.assign(N, 0.0);
            out.noise_added_W.assign(N, 0.0);
            out.noise_total_W.assign(N, 0.0);
            out.bumpGeneration();
            return;
        }

        // Amplify input noise by |S21|^2 and add the device noise per bin, each bin scaled
        // by its own gain. A grid-wide mean would let clamped and negative bins dominate.
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        for (size_t i = 0; i < N; ++i) {
            auto S = m_sparam_data.interpolate(out.frequencies[i], m_sparam_fwd_idx);
            double gain_linear = std::norm(S);
            double nin =
                (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
            out.noise_W[i] = gain_linear * nin;
            double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, gain_linear);
            out.noise_added_W[i] = added_density <= 0.0 ? 0.0 : added_density;
        }

        out.noise_total_W.resize(N);
        for (size_t i = 0; i < N; ++i)
            out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];

        out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
        out.bumpGeneration();
        return;
    }

    m_dirty = false;
    m_cached_input_ptr = in_ptr;
    if (in_ptr)
        m_cached_input_generation = in_ptr->generation;

    auto &out = m_node.outputs[0];

    if (in_ptr && !in_ptr->frequencies.empty()) {
        out.frequencies = in_ptr->frequencies;
    } else if (out.frequencies.size() < 2) {
        buildDefaultFrequencyGrid(out.frequencies);
    }

    out.tones = in_ptr ? in_ptr->tones : std::vector<Spectrum::Tone>{};
    out.is_complex_baseband = in_ptr ? in_ptr->is_complex_baseband : false;
    out.fs_Hz = in_ptr ? in_ptr->fs_Hz : 0.0;
    out.out_of_band = in_ptr ? in_ptr->out_of_band : std::vector<bool>{};
    for (auto &t : out.tones) {
        t.power_dBm += m_gain_dB;
    }

    // Nonlinear processing
    if (m_nonlinear.enabled() && in_ptr && !in_ptr->tones.empty()) {
        size_t n_fund = out.tones.size();
        double gain_amp = std::sqrt(dbToLinear(m_gain_dB));
        const NonlinearModel::GainFn gain = [gain_amp](double) { return gain_amp; };
        auto result = m_nonlinear.process(in_ptr->tones, gain, intercept3Function(gain),
                                          intercept2Function(gain));

        for (const auto &t : result.extra_tones)
            out.tones.push_back(t);

        for (size_t i = 0; i < n_fund; ++i)
            out.tones[i].power_dBm += result.tone_compression_dB[i];
    }

    const size_t N = out.frequencies.size();

    if (in_ptr && !in_ptr->phase_deg.empty()) {
        out.phase_deg = in_ptr->phase_deg;
    } else {
        out.phase_deg.assign(N, 0.0);
    }
    if (N < 2) {
        out.noise_W.assign(N, 0.0);
        out.noise_added_W.assign(N, 0.0);
        out.noise_total_W.assign(N, 0.0);
        out.phase_deg.assign(N, 0.0);
        out.bumpGeneration();
        return;
    }

    double G = dbToLinear(m_gain_dB);
    double added_density = addedNoiseDensity_W_per_Hz(m_nf_dB, G);

    out.noise_W.assign(N, 0.0);
    for (size_t i = 0; i < N; ++i) {
        double nin = (in_ptr && i < in_ptr->noise_total_W.size() ? in_ptr->noise_total_W[i] : 0.0);
        out.noise_W[i] = G * nin;
    }

    out.noise_added_W.resize(N);
    if (added_density <= 0.0) {
        out.noise_added_W.assign(N, 0.0);
    } else {
        out.noise_added_W.assign(N, added_density);
    }
    out.noise_total_W.resize(N);
    for (size_t i = 0; i < N; ++i) {
        out.noise_total_W[i] = out.noise_W[i] + out.noise_added_W[i];
    }

    out.bumpGeneration();
}

nlohmann::json AmplifierEngine::serialize() const {
    return {{"gain_dB", m_gain_dB},
            {"nf_dB", m_nf_dB},
            {"enable_nonlinear", m_nonlinear.enabled()},
            {"oip2_dBm", m_nonlinear.oip2_dBm()},
            {"oip3_dBm", m_nonlinear.oip3_dBm()},
            {"p1db_dBm", m_nonlinear.p1db_dBm()},
            {"sparam_mode", m_sparam_mode},
            {"sparam_filepath", m_sparam_filepath},
            {"sparam_fwd_idx", m_sparam_fwd_idx},
            {"amp_filepath", m_amp_filepath},
            {"ampf_filepath", m_ampf_filepath},
            {"ampf_block", selectionToJson(m_ampf_selection)}};
}

void AmplifierEngine::deserialize(const nlohmann::json &j) {
    m_gain_dB = j.value("gain_dB", 0.0);
    m_nf_dB = j.value("nf_dB", 0.0);
    m_nonlinear.setEnabled(j.value("enable_nonlinear", false));
    m_nonlinear.setOIP2_dBm(j.value("oip2_dBm", 100.0));
    m_nonlinear.setOIP3_dBm(j.value("oip3_dBm", 100.0));
    m_nonlinear.setP1dB_dBm(j.value("p1db_dBm", 100.0));
    // Library definitions (schema v1/v2) omit `enable_nonlinear` but include
    // OIP/P1dB params. The old registry factory enabled nonlinearity whenever
    // any of those were present; project files always serialize the explicit
    // key, so only fall back when it is absent.
    if (!j.contains("enable_nonlinear") &&
        (j.contains("oip2_dBm") || j.contains("oip3_dBm") || j.contains("p1db_dBm")))
        m_nonlinear.setEnabled(true);
    m_sparam_filepath = j.value("sparam_filepath", j.value("sparam_path", ""));
    if (!m_sparam_filepath.empty())
        m_sparam_data.load(m_sparam_filepath);
    m_sparam_mode =
        j.value("sparam_mode", false) && m_sparam_data.loaded() && m_sparam_data.numPorts() >= 2;
    // An absent index selects S21 for a loaded file, as setSParamFilepath() does.
    const int default_fwd_idx = m_sparam_data.loaded() ? m_sparam_data.numPorts() : 0;
    m_sparam_fwd_idx = j.value("sparam_fwd_idx", default_fwd_idx);
    m_ampf_selection = selectionFromJson(j.value("ampf_block", nlohmann::json::object()));
    loadAmpFile(j.value("amp_filepath", std::string{}));
    loadAmpfFile(j.value("ampf_filepath", std::string{}));
    m_dirty = true;
}

std::string AmplifierEngine::hoverSummary() const {
    if (m_sparam_mode && m_sparam_data.loaded()) {
        return "S-Param Amp | NF: " + std::to_string(m_nf_dB) + " dB" +
               (m_nonlinear.enabled() ? " | NL On" : "");
    }
    return "Gain: " + std::to_string(m_gain_dB) + " dB | NF: " + std::to_string(m_nf_dB) + " dB";
}
