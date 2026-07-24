#pragma once

#include "spectrum.h"
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
                m_oip3_dBm = m_p1db_dBm + 9.6;
                if (m_enabled)
                    recomputeCoefficients();
            }
        }
    }

    struct Result {
        std::vector<Spectrum::Tone> extra_tones;
        double compression_dB = 0.0;
    };

    Result process(const std::vector<Spectrum::Tone> &input_tones, GainFn gain_at_freq) const {
        using detail::dbmToV;
        using detail::dbmToW;
        using detail::vToDbm;
        using detail::wToDbm;

        Result r;
        if (!m_enabled || input_tones.empty())
            return r;

        double total_distortion_mW = 0.0;

        // Harmonics (from each input tone)
        for (const auto &tone : input_tones) {
            double g_linear = gain_at_freq(tone.freq_Hz);
            double Pout_dBm = tone.power_dBm + 20.0 * std::log10(g_linear);
            double Vp1 = dbmToV(Pout_dBm);

            double V_h2 = m_k1 * Vp1 / std::sqrt(2.0);
            double H2_dBm = vToDbm(V_h2);
            r.extra_tones.push_back({tone.freq_Hz * 2.0, H2_dBm, 0.0});
            total_distortion_mW += dbmToW(H2_dBm);

            double V_h3 = m_k2 * Vp1 * Vp1 * Vp1 / 4.0;
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

                double V_im2 = m_k1 * Vp1 * Vp2;
                double IM2_dBm = vToDbm(V_im2);
                r.extra_tones.push_back({std::abs(f1 - f2), IM2_dBm, 0.0});
                r.extra_tones.push_back({f1 + f2, IM2_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM2_dBm);

                double V_im3_12 = (3.0 / 4.0) * m_k2 * Vp1 * Vp1 * Vp2;
                double IM3_12_dBm = vToDbm(V_im3_12);
                r.extra_tones.push_back({2.0 * f1 + f2, IM3_12_dBm, 0.0});
                r.extra_tones.push_back({std::abs(2.0 * f1 - f2), IM3_12_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM3_12_dBm);

                double V_im3_21 = (3.0 / 4.0) * m_k2 * Vp1 * Vp2 * Vp2;
                double IM3_21_dBm = vToDbm(V_im3_21);
                r.extra_tones.push_back({2.0 * f2 + f1, IM3_21_dBm, 0.0});
                r.extra_tones.push_back({std::abs(f1 - 2.0 * f2), IM3_21_dBm, 0.0});
                total_distortion_mW += 2.0 * dbmToW(IM3_21_dBm);
            }
        }

        // Compression
        double Pfund_mW = 0.0;
        for (const auto &tone : input_tones) {
            double g = gain_at_freq(tone.freq_Hz);
            double Pout = tone.power_dBm + 20.0 * std::log10(g);
            Pfund_mW += dbmToW(Pout);
        }

        if (total_distortion_mW >= Pfund_mW || Pfund_mW <= 0.0) {
            r.compression_dB = -1e9; // deep compression sentinel
        } else {
            double ratio = 1.0 - total_distortion_mW / Pfund_mW;
            r.compression_dB = 10.0 * std::log10(ratio);
        }

        return r;
    }

  private:
    bool m_enabled = false;
    double m_oip2_dBm = 100.0;
    double m_oip3_dBm = 100.0;
    double m_p1db_dBm = 100.0;
    double m_k1 = 0.0;
    double m_k2 = 0.0;

    void recomputeCoefficients() {
        using detail::dbmToV;
        double V_oip2 = dbmToV(m_oip2_dBm);
        double V_oip3 = dbmToV(m_oip3_dBm);
        m_k1 = 1.0 / V_oip2;
        m_k2 = 4.0 / (3.0 * V_oip3 * V_oip3);
    }
};
