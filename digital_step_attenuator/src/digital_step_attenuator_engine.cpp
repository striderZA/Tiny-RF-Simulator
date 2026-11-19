#include "digital_step_attenuator_engine.h"
#include <algorithm>
#include <cmath>

DigitalStepAttenuatorEngine::DigitalStepAttenuatorEngine(int id, NodeGraphEngine &graph)
    : AttenuatorEngine(id, graph, "Digital Step Attenuator") {
    // The default state (ZX76-31R5A-PN+: 1.4 dB at code 0) is already a
    // non-zero attenuation, so seed the base engine instead of leaving 0 dB
    // until the first setter call.
    applyAttenuation();
}

int DigitalStepAttenuatorEngine::maxCode() const { return (1 << m_bit_depth) - 1; }

double DigitalStepAttenuatorEngine::totalAttenuation_dB() const {
    return m_ref_loss_dB + m_code * m_step_dB;
}

void DigitalStepAttenuatorEngine::applyAttenuation() { setAttenuation(totalAttenuation_dB()); }

void DigitalStepAttenuatorEngine::setBitDepth(int bits) {
    m_bit_depth = std::clamp(bits, 1, MAX_BIT_DEPTH);
    m_code = std::clamp(m_code, 0, maxCode());
    applyAttenuation();
}

void DigitalStepAttenuatorEngine::setStepSize(double dB) {
    m_step_dB = std::clamp(dB, 0.0, MAX_STEP_DB);
    applyAttenuation();
}

void DigitalStepAttenuatorEngine::setRefInsertionLoss(double dB) {
    m_ref_loss_dB = std::clamp(dB, 0.0, MAX_REF_LOSS_DB);
    applyAttenuation();
}

void DigitalStepAttenuatorEngine::setCode(int code) {
    m_code = std::clamp(code, 0, maxCode());
    applyAttenuation();
}

nlohmann::json DigitalStepAttenuatorEngine::serialize() const {
    return {{"bit_depth", m_bit_depth},
            {"step_dB", m_step_dB},
            {"ref_loss_dB", m_ref_loss_dB},
            {"code", m_code}};
}

void DigitalStepAttenuatorEngine::deserialize(const nlohmann::json &j) {
    // Accept any JSON number for the integer fields: the library apply path
    // hands raw authoring JSON to deserialize(), so a whole-number float
    // (6.0) must not be silently dropped for being the wrong JSON type.
    if (j.contains("bit_depth") && j["bit_depth"].is_number())
        m_bit_depth = std::clamp(static_cast<int>(std::llround(j["bit_depth"].get<double>())), 1,
                                 MAX_BIT_DEPTH);
    if (j.contains("step_dB") && j["step_dB"].is_number())
        m_step_dB = std::clamp(j["step_dB"].get<double>(), 0.0, MAX_STEP_DB);
    if (j.contains("ref_loss_dB") && j["ref_loss_dB"].is_number())
        m_ref_loss_dB = std::clamp(j["ref_loss_dB"].get<double>(), 0.0, MAX_REF_LOSS_DB);
    // The code clamps against the (possibly just changed) bit depth last.
    if (j.contains("code") && j["code"].is_number())
        m_code = std::clamp(static_cast<int>(std::llround(j["code"].get<double>())), 0, maxCode());
    else
        m_code = std::clamp(m_code, 0, maxCode());
    applyAttenuation();
}

std::string DigitalStepAttenuatorEngine::hoverSummary() const {
    return "Step Attn: " + std::to_string(totalAttenuation_dB()) + " dB (code " +
           std::to_string(m_code) + "/" + std::to_string(maxCode()) + ")";
}
