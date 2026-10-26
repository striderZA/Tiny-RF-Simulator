#pragma once

#include "spectrum.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace detail {

inline double dbmToW(double dBm) { return std::pow(10.0, dBm / 10.0) * 0.001; }
inline double wToDbm(double W) { return 10.0 * std::log10(W / 0.001); }
inline double dbmToV(double dBm) { return std::sqrt(dbmToW(dBm) * 50.0); }
inline double vToDbm(double V) { return 10.0 * std::log10((V * V / 50.0) / 0.001); }

} // namespace detail

class NonlinearModel {
  public:
    using GainFn = std::function<double(double freq_Hz)>;
    // Output intercepts (dBm) at a frequency, for intercepts that vary with frequency.
    using Oip3Fn = std::function<double(double freq_Hz)>;
    using Oip2Fn = std::function<double(double freq_Hz)>;

    // While OIP3 is at its default, setting P1dB sets OIP3 this many dB above it.
    static constexpr double kOip3OverP1dB_dB = 9.6;

    void setOIP2_dBm(double oip2) {
        if (oip2 != m_oip2_dBm) {
            m_oip2_dBm = oip2;
            if (m_enabled)
                recomputeCoefficients();
        }
    }
    void setOIP3_dBm(double oip3) {
        if (oip3 != m_oip3_dBm) {
            m_oip3_dBm = oip3;
            if (m_enabled)
                recomputeCoefficients();
        }
    }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool en) {
        if (en != m_enabled) {
            m_enabled = en;
            if (en)
                recomputeCoefficients();
        }
    }

    double oip2_dBm() const { return m_oip2_dBm; }
    double oip3_dBm() const { return m_oip3_dBm; }
    double p1db_dBm() const { return m_p1db_dBm; }
    void setP1dB_dBm(double p1db) {
        if (p1db != m_p1db_dBm) {
            m_p1db_dBm = p1db;
            // If OIP3 is default (100), derive from P1dB
            if (m_oip3_dBm >= 99.0 && m_p1db_dBm < 90.0) {
                m_oip3_dBm = m_p1db_dBm + kOip3OverP1dB_dB;
                if (m_enabled)
                    recomputeCoefficients();
            }
        }
    }

    struct Result {
        std::vector<Spectrum::Tone> extra_tones;
        double compression_dB = 0.0;
    };

    // Products use the scalar intercepts unless functions are given. With oip3_at_freq, each
    // harmonic uses the OIP3 at the frequency of the tone it comes from, and each IM3 product
    // uses the OIP3 at the fundamental it squares: 2f1 +- f2 uses f1, 2f2 +- f1 uses f2. With
    // oip2_at_freq, each harmonic uses the OIP2 at its own tone, and each IM2 product uses the
    // lower of its two tones' OIP2, the larger second-order coefficient. A non-finite value from
    // either function is replaced by the scalar intercept.
    // compression_dB is 20*log10(1 - Pout/OIP3) for the tone with the largest Pout/OIP3, using the
    // OIP3 at that tone's frequency. Its headroom is floored at 1/sqrt(2): it never falls below
    // -3.01 dB.
    Result process(const std::vector<Spectrum::Tone> &input_tones, GainFn gain_at_freq,
                   const Oip3Fn &oip3_at_freq = {}, const Oip2Fn &oip2_at_freq = {}) const {
        using detail::dbmToV;
        using detail::dbmToW;
        using detail::vToDbm;
        using detail::wToDbm;

        Result r;
        if (!m_enabled || input_tones.empty())
            return r;

        double total_distortion_mW = 0.0;

        // Second-order coefficient for a fundamental at freq_Hz.
        const auto k1_at = [&](double freq_Hz) {
            if (!oip2_at_freq)
                return m_k1;
            const double k = k1ForOip2(oip2_at_freq(freq_Hz));
            return std::isfinite(k) ? k : m_k1;
        };
        // Output third-order intercept (dBm) for a fundamental at freq_Hz.
        const auto oip3_dBm_at = [&](double freq_Hz) {
            if (!oip3_at_freq)
                return m_oip3_dBm;
            const double oip3 = oip3_at_freq(freq_Hz);
            return std::isfinite(oip3) ? oip3 : m_oip3_dBm;
        };
        // Third-order coefficient for a fundamental at freq_Hz.
        const auto k2_at = [&](double freq_Hz) {
            if (!oip3_at_freq)
                return m_k2;
            const double k = k2ForOip3(oip3_dBm_at(freq_Hz));
            return std::isfinite(k) ? k : m_k2;
        };

        // Harmonics (from each input tone)
        for (const auto &tone : input_tones) {
            double g_linear = gain_at_freq(tone.freq_Hz);
            double Pout_dBm = tone.power_dBm + 20.0 * std::log10(g_linear);
            double Vp1 = dbmToV(Pout_dBm);

            // H2: v_in^2 term -> RMS k1*A^2/(2*sqrt(2)) = k1*Vp1^2/sqrt(2), A = sqrt(2)*Vp1
            double V_h2 = k1_at(tone.freq_Hz) * Vp1 * Vp1 / std::sqrt(2.0);
            double H2_dBm = vToDbm(V_h2);
            r.extra_tones.push_back({tone.freq_Hz * 2.0, H2_dBm, 0.0});
            total_distortion_mW += dbmToW(H2_dBm);

            // H3: one third the amplitude of equal-power IM3 for a cubic term.
            double V_h3 = k2_at(tone.freq_Hz) * Vp1 * Vp1 * Vp1 / 4.0;
            double H3_dBm = vToDbm(V_h3);
            r.extra_tones.push_back({tone.freq_Hz * 3.0, H3_dBm, 0.0});
            total_distortion_mW += dbmToW(H3_dBm);
        }

        // IMD (from unique tone pairs, cap at 3 tones)
        int n_tones = std::min(static_cast<int>(input_tones.size()), 3);
        for (int i = 0; i < n_tones; ++i) {
            for (int j = i + 1; j < n_tones; ++j) {
                double g1 = gain_at_freq(input_tones[i].freq_Hz);
                double g2 = gain_at_freq(input_tones[j].freq_Hz);
                double P1 = input_tones[i].power_dBm + 20.0 * std::log10(g1);
                double P2 = input_tones[j].power_dBm + 20.0 * std::log10(g2);
                double f1 = input_tones[i].freq_Hz;
                double f2 = input_tones[j].freq_Hz;
                double Vp1 = dbmToV(P1);
                double Vp2 = dbmToV(P2);

                // IM2 uses the larger of the two second-order coefficients (lower intercept).
                // Worst case by design: the cross term is not derived from the model.
                const double k1_im2 = std::max(k1_at(f1), k1_at(f2));
                double V_im2 = k1_im2 * Vp1 * Vp2;
                double IM2_dBm = vToDbm(V_im2);
                r.extra_tones.push_back({std::abs(f1 - f2), IM2_dBm, 0.0});
                r.extra_tones.push_back({f1 + f2, IM2_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM2_dBm);

                double V_im3_12 = (3.0 / 4.0) * k2_at(f1) * Vp1 * Vp1 * Vp2;
                double IM3_12_dBm = vToDbm(V_im3_12);
                r.extra_tones.push_back({2.0 * f1 + f2, IM3_12_dBm, 0.0});
                r.extra_tones.push_back({std::abs(2.0 * f1 - f2), IM3_12_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM3_12_dBm);

                double V_im3_21 = (3.0 / 4.0) * k2_at(f2) * Vp1 * Vp2 * Vp2;
                double IM3_21_dBm = vToDbm(V_im3_21);
                r.extra_tones.push_back({2.0 * f2 + f1, IM3_21_dBm, 0.0});
                r.extra_tones.push_back({std::abs(f1 - 2.0 * f2), IM3_21_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM3_21_dBm);
            }
        }

        // Compression. Distortion is capped at half the fundamental: past that point every product
        // is scaled by one factor so total distortion is exactly half the fundamental. The cap
        // keeps every spur at or below the fundamental.
        double Pfund_mW = 0.0;
        double worst_ratio = 0.0; // largest Pout / OIP3 (linear) over the input tones
        for (const auto &tone : input_tones) {
            double g = gain_at_freq(tone.freq_Hz);
            double Pout = tone.power_dBm + 20.0 * std::log10(g);
            Pfund_mW += dbmToW(Pout);
            worst_ratio =
                std::max(worst_ratio, std::pow(10.0, (Pout - oip3_dBm_at(tone.freq_Hz)) / 10.0));
        }

        if (Pfund_mW <= 0.0) {
            r.compression_dB = 0.0; // no fundamental power, nothing to compress
            return r;
        }
        if (total_distortion_mW > 0.5 * Pfund_mW) {
            const double scale = 0.5 * Pfund_mW / total_distortion_mW;
            for (auto &product : r.extra_tones)
                product.power_dBm += 10.0 * std::log10(scale);
            total_distortion_mW *= scale;
        }
        // Closed-form cubic for the most compressed tone. The headroom is floored at 1/sqrt(2), the
        // -3.01 dB half-fundamental floor, so compression stays finite past the 1 dB point.
        r.compression_dB = 20.0 * std::log10(std::max(1.0 - worst_ratio, std::sqrt(0.5)));

        return r;
    }

  private:
    bool m_enabled = false;
    double m_oip2_dBm = 100.0;
    double m_oip3_dBm = 100.0;
    double m_p1db_dBm = 100.0;
    double m_k1 = 0.0;
    double m_k2 = 0.0;

    // Second-order coefficient for an output intercept (dBm).
    static double k1ForOip2(double oip2_dBm) { return 1.0 / detail::dbmToV(oip2_dBm); }

    // Third-order coefficient for an output intercept (dBm).
    static double k2ForOip3(double oip3_dBm) {
        const double V_oip3 = detail::dbmToV(oip3_dBm);
        return 4.0 / (3.0 * V_oip3 * V_oip3);
    }

    void recomputeCoefficients() {
        m_k1 = k1ForOip2(m_oip2_dBm);
        m_k2 = k2ForOip3(m_oip3_dBm);
    }
};
