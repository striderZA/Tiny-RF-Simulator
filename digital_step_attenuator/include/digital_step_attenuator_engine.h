#pragma once

#include "attenuator_engine.h"

// Digital step attenuator, modeled on parts like the Mini-Circuits
// ZX76-31R5A-PN+: an integer control code selects attenuation in fixed
// steps on top of a zero-code reference insertion loss. The applied
// attenuation is
//   atten_dB = ref_loss_dB + code * step_dB,  code in [0, 2^bit_depth - 1].
// The DSP is the attenuator's own passive model, inherited unchanged:
// tones drop by atten_dB and each bin's noise is
//   noise_total = G*noise_in + k*T*(1 - G),  G = 10^(-atten_dB/10).
//
// Defaults are the ZX76-31R5A-PN+ datasheet values: 6 bits, 0.5 dB step,
// 1.4 dB typ insertion loss at zero code (DC-1 GHz; 2.0 dB max, 1.9 dB typ
// at 1-2.4 GHz, 2.5 dB typ at 2.4-4 GHz).
class DigitalStepAttenuatorEngine : public AttenuatorEngine {
  public:
    DigitalStepAttenuatorEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "digital_step_attenuator"; }
    std::string hoverSummary() const override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &j) override;

    void setBitDepth(int bits);
    int bitDepth() const { return m_bit_depth; }
    int maxCode() const;

    void setStepSize(double dB);
    double stepSize_dB() const { return m_step_dB; }

    void setRefInsertionLoss(double dB);
    double refInsertionLoss_dB() const { return m_ref_loss_dB; }

    void setCode(int code);
    int code() const { return m_code; }

    double totalAttenuation_dB() const;

    static constexpr int MAX_BIT_DEPTH = 16;
    static constexpr double MAX_STEP_DB = 20.0;
    static constexpr double MAX_REF_LOSS_DB = 30.0;

  private:
    // Pushes totalAttenuation_dB() into the attenuator base, which clamps
    // it into [0, 200] dB and marks the engine dirty.
    void applyAttenuation();

    int m_bit_depth = 6;
    double m_step_dB = 0.5;
    double m_ref_loss_dB = 1.4;
    int m_code = 0;
};
