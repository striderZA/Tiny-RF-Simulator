#include "spectrum_analyzer_im3.h"
#include "spectrum_analyzer_trace.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace st = spectrum_trace;

namespace {

// A real-domain trace mirrors every tone at +-f with the same level, and the positive image
// is the one Peak and the markers name. A one-sided trace has no such image.
constexpr double kMirrorLevelTolerance_dB = 1.0;

struct Candidate {
    size_t index = 0;
    double frequency_Hz = 0.0;
    double level_dBm = 0.0;
};

bool strongerFirst(const Candidate &a, const Candidate &b) {
    if (std::abs(a.level_dBm - b.level_dBm) > 1e-9)
        return a.level_dBm > b.level_dBm;
    return a.frequency_Hz < b.frequency_Hz;
}

} // namespace

void Im3Model::measure(const std::vector<double> &freq_axis, const std::vector<double> &trace_dBm,
                       double span_start_Hz, double span_stop_Hz, double rbw_Hz,
                       std::optional<std::pair<double, double>> tones_Hz) {
    m_result = {};
    m_result.products[0].high_side = false;
    m_result.products[1].high_side = true;

    const double width = st::binWidth(freq_axis);
    if (freq_axis.size() != trace_dBm.size() || width <= 0.0) {
        for (auto &product : m_result.products)
            product.status = Status::NeedTwoTones;
        return;
    }

    const double threshold = st::medianLevel(trace_dBm) + kPeakClearance_dB;
    const double rbw = std::isfinite(rbw_Hz) ? std::max(0.0, rbw_Hz) : 0.0;
    // One resolution bandwidth wide, widened to a bin so a narrow RBW still finds a peak.
    const double half_window = std::max(0.5 * rbw, width);

    if (tones_Hz) {
        const auto [first, second] = *tones_Hz;
        if (!std::isfinite(first) || !std::isfinite(second) || first < span_start_Hz ||
            first > span_stop_Hz || second < span_start_Hz || second > span_stop_Hz) {
            m_result.tone_status = Status::TonesOutOfSpan;
        } else if (first == 0.0 || second == 0.0 || first == second) {
            // 0 Hz is dc, not a usable RF fundamental, and one frequency cannot form a pair.
            m_result.tone_status = Status::NeedTwoTones;
        } else {
            const double lower = std::min(first, second);
            const double upper = std::max(first, second);
            const int lower_index =
                st::nearestInSpan(freq_axis, trace_dBm, lower, span_start_Hz, span_stop_Hz);
            const int upper_index =
                st::nearestInSpan(freq_axis, trace_dBm, upper, span_start_Hz, span_stop_Hz);
            if (lower_index < 0 || upper_index < 0) {
                m_result.tone_status = Status::NeedTwoTones;
            } else {
                // The user placed the markers, so their frequencies are taken as given.
                m_result.tone_status = Status::Measured;
                m_result.tones[0] = {lower, trace_dBm[static_cast<size_t>(lower_index)]};
                m_result.tones[1] = {upper, trace_dBm[static_cast<size_t>(upper_index)]};
            }
        }
    } else {
        const std::ptrdiff_t dip_bins = st::dipBins(rbw, width);
        std::vector<Candidate> candidates;
        for (size_t i = 0; i < freq_axis.size(); ++i) {
            const double frequency = freq_axis[i];
            if (!std::isfinite(frequency) || frequency == 0.0 || frequency < span_start_Hz ||
                frequency > span_stop_Hz) {
                continue;
            }
            if (!st::isClearPeak(trace_dBm, i, threshold, dip_bins))
                continue;
            candidates.push_back({i, frequency, trace_dBm[i]});
        }

        // Drop a negative peak that mirrors a near-equal positive peak.
        std::vector<Candidate> tones;
        tones.reserve(candidates.size());
        for (const Candidate &candidate : candidates) {
            bool mirrored = false;
            if (candidate.frequency_Hz < 0.0) {
                for (const Candidate &positive : candidates) {
                    if (positive.frequency_Hz <= 0.0)
                        continue;
                    if (std::abs(positive.frequency_Hz + candidate.frequency_Hz) <= width &&
                        std::abs(positive.level_dBm - candidate.level_dBm) <=
                            kMirrorLevelTolerance_dB) {
                        mirrored = true;
                        break;
                    }
                }
            }
            if (!mirrored)
                tones.push_back(candidate);
        }

        std::sort(tones.begin(), tones.end(), strongerFirst);
        if (tones.size() < 2) {
            m_result.tone_status = Status::NeedTwoTones;
        } else {
            const Candidate &primary = tones.front();
            const Candidate *partner = nullptr;
            for (size_t i = 1; i < tones.size(); ++i) {
                if (std::abs(tones[i].frequency_Hz - primary.frequency_Hz) > width) {
                    partner = &tones[i];
                    break;
                }
            }
            if (!partner) {
                m_result.tone_status = Status::NeedTwoTones;
            } else {
                m_result.tone_status = Status::Measured;
                const Candidate *lower = &primary;
                const Candidate *upper = partner;
                if (lower->frequency_Hz > upper->frequency_Hz)
                    std::swap(lower, upper);
                m_result.tones[0] = {lower->frequency_Hz, lower->level_dBm};
                m_result.tones[1] = {upper->frequency_Hz, upper->level_dBm};
            }
        }
    }

    if (m_result.tone_status != Status::Measured) {
        for (auto &product : m_result.products)
            product.status = m_result.tone_status;
        return;
    }

    const double f1 = m_result.tones[0].frequency_Hz;
    const double f2 = m_result.tones[1].frequency_Hz;
    const double spacing = f2 - f1;
    // A product sits one tone spacing from its fundamental; an RBW that wide blurs them.
    const bool unresolved = half_window >= 0.5 * spacing;

    double intercept_sum = 0.0;
    int intercept_count = 0;
    for (auto &product : m_result.products) {
        product.expected_Hz = product.high_side ? 2.0 * f2 - f1 : 2.0 * f1 - f2;
        product.frequency_Hz = product.expected_Hz;

        if (product.expected_Hz < span_start_Hz || product.expected_Hz > span_stop_Hz) {
            product.status = Status::OutOfSpan;
            continue;
        }
        if (unresolved) {
            product.status = Status::Unresolved;
            product.rbw_limited = 0.5 * rbw >= 0.5 * spacing;
            continue;
        }

        const int index = st::strongestInWindow(freq_axis, trace_dBm, product.expected_Hz,
                                                half_window, span_start_Hz, span_stop_Hz);
        if (index < 0 || !(trace_dBm[static_cast<size_t>(index)] > threshold)) {
            product.status = Status::NotFound;
            continue;
        }
        product.status = Status::Measured;
        product.frequency_Hz = freq_axis[static_cast<size_t>(index)];
        product.amplitude_dBm = trace_dBm[static_cast<size_t>(index)];
        const double nearest_tone_dBm = m_result.tones[product.high_side ? 1 : 0].amplitude_dBm;
        product.relative_dBc = product.amplitude_dBm - nearest_tone_dBm;
        // Cubic IM3: P_IM3 = 2*P1 + P2 - 2*OIP3, so OIP3 = (P1 + P2 - dBc) / 2.
        product.intercept_dBm = (m_result.tones[0].amplitude_dBm + m_result.tones[1].amplitude_dBm -
                                 product.relative_dBc) /
                                2.0;
        intercept_sum += product.intercept_dBm;
        ++intercept_count;
    }

    if (intercept_count > 0) {
        m_result.toi_valid = true;
        m_result.toi_dBm = intercept_sum / static_cast<double>(intercept_count);
    }
}
