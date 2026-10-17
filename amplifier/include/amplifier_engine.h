#pragma once

#include "amp_file.h"
#include "ampf_file.h"
#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "nonlinear_model.h"
#include "s_parameter_data.h"
#include "signal_node.h"
#include <algorithm>
#include <optional>

class AmplifierEngine : public ComponentEngineBase {
  public:
    AmplifierEngine(int id, NodeGraphEngine &graph);
    std::string_view type_name() const override { return "amplifier"; }
    std::string hoverSummary() const override;

    void setGain_dB(double g) {
        if (g != m_gain_dB) {
            m_gain_dB = g;
            m_dirty = true;
        }
    }
    void setNF_dB(double nf) {
        double clamped = std::max(0.0, nf);
        if (clamped != m_nf_dB) {
            m_nf_dB = clamped;
            m_dirty = true;
        }
    }
    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    double gain_dB() const { return m_gain_dB; }
    double nf_dB() const { return m_nf_dB; }
    bool enableNonlinear() const { return m_nonlinear.enabled(); }
    double oip2_dBm() const { return m_nonlinear.oip2_dBm(); }
    double oip3_dBm() const { return m_nonlinear.oip3_dBm(); }
    double p1db_dBm() const { return m_nonlinear.p1db_dBm(); }

    void setEnableNonlinear(bool en) {
        if (en != m_nonlinear.enabled()) {
            m_nonlinear.setEnabled(en);
            m_dirty = true;
        }
    }
    void setOIP2_dBm(double oip2) {
        m_nonlinear.setOIP2_dBm(std::max(-30.0, oip2));
        if (m_nonlinear.enabled())
            m_dirty = true;
    }
    void setOIP3_dBm(double oip3) {
        m_nonlinear.setOIP3_dBm(std::max(-30.0, oip3));
        if (m_nonlinear.enabled())
            m_dirty = true;
    }
    void setP1dB_dBm(double p1db) {
        m_nonlinear.setP1dB_dBm(std::max(-30.0, p1db));
        if (m_nonlinear.enabled())
            m_dirty = true;
    }

    // S-parameter mode
    void setSParamFilepath(const std::string &path);
    bool sparamMode() const { return m_sparam_mode; }
    void setSParamMode(bool en) {
        m_sparam_mode = en && (!m_sparam_data.loaded() || m_sparam_data.numPorts() >= 2);
        m_dirty = true;
    }
    bool sparamLoaded() const { return m_sparam_data.loaded(); }
    const std::string &sparamFilepath() const { return m_sparam_filepath; }
    const SParameterData &sparamData() const { return m_sparam_data; }

    // AMP intercept file (MathWorks AMP, IIP3/OIP3 sections). While a file is loaded, its
    // curves replace the scalar OIP3, unless an amp-f block is active. A file that fails to load
    // keeps its path and error, and the scalar OIP3 applies.
    void setAmpFilepath(const std::string &path);
    const std::string &ampFilepath() const { return m_amp_filepath; }
    bool ampLoaded() const { return m_amp.has_value(); }
    const AmpFile *ampData() const { return m_amp ? &*m_amp : nullptr; }
    const std::string &ampError() const { return m_amp_error; }

    // amp-f v1 file: per-frequency OIP2, OIP3, and OP1dB for the block whose VAR and COND values
    // match the selection. While a block is active, its curves replace the legacy AMP file's
    // intercepts. A frequency with no finite value uses the scalar. A file that fails to load,
    // or a selection that does not resolve to one block, keeps its path and error, and no block
    // is active. Then OIP3 comes from the legacy AMP file if one is loaded, and otherwise from
    // the scalar. OIP2 comes from the scalar.
    void setAmpfFilepath(const std::string &path);
    const std::string &ampfFilepath() const { return m_ampf_filepath; }
    bool ampfLoaded() const { return m_ampf.has_value(); }
    const AmpfFile *ampfData() const { return m_ampf ? &*m_ampf : nullptr; }
    const std::string &ampfError() const { return m_ampf_error; }
    // Exact VAR and COND values that pick one block. An empty selection matches a file with
    // one block.
    void setAmpfSelection(const AmpfValues &wanted);
    const AmpfValues &ampfSelection() const { return m_ampf_selection; }
    bool ampfBlockSelected() const { return m_ampf_block >= 0; }

  private:
    double m_gain_dB = 0.0;
    double m_nf_dB = 0.0;
    NonlinearModel m_nonlinear;

    // S-parameter state
    SParameterData m_sparam_data;
    std::string m_sparam_filepath;
    bool m_sparam_mode = false;
    int m_sparam_fwd_idx = 0;
    const Spectrum *m_cached_sparam_input = nullptr;
    uint64_t m_cached_sparam_generation = 0;

    // AMP intercept state
    std::string m_amp_filepath;
    std::optional<AmpFile> m_amp;
    std::string m_amp_error;

    // amp-f intercept state
    std::string m_ampf_filepath;
    std::optional<AmpfFile> m_ampf;
    std::string m_ampf_error;
    AmpfValues m_ampf_selection;
    int m_ampf_block = -1; // index into m_ampf->blocks; -1 when no block is active

    void loadAmpFile(const std::string &path);
    void loadAmpfFile(const std::string &path);
    void resolveAmpfBlock();
    const AmpfBlock *activeAmpfBlock() const {
        return ampfBlockSelected() ? &m_ampf->blocks[static_cast<size_t>(m_ampf_block)] : nullptr;
    }
    NonlinearModel::Oip3Fn intercept3Function(const NonlinearModel::GainFn &gain) const;
    NonlinearModel::Oip3Fn ampfOip3Function(const NonlinearModel::GainFn &gain) const;
    NonlinearModel::Oip2Fn intercept2Function(const NonlinearModel::GainFn &gain) const;
    NonlinearModel::Oip2Fn ampfOip2Function(const NonlinearModel::GainFn &gain) const;
};
