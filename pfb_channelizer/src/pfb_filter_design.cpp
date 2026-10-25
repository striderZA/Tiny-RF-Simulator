#define _USE_MATH_DEFINES
#include "pfb_filter_design.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
double kaiserWindowAt(double index, int N, double beta) {
    // Symmetric Kaiser window over taps 0..N-1. index in [0, N-1].
    const double x = 2.0 * index / (N - 1) - 1.0; // -1..1
    if (std::abs(x) > 1.0)
        return 0.0;
    const double arg = beta * std::sqrt(std::max(0.0, 1.0 - x * x));
    return std::cyl_bessel_i(0, arg) / std::cyl_bessel_i(0, beta);
}
double sincP(double v) { return std::abs(v) < 1e-12 ? 1.0 : std::sin(M_PI * v) / (M_PI * v); }
double toDb(double v) { return 20.0 * std::log10(std::max(v, 1e-300)); }
} // namespace

PfbFilterDesign::PfbFilterDesign(int M, int K, double beta) {
    m_M = std::clamp(M, 2, 2048);
    m_K = std::clamp(K, 1, 64);
    m_beta = std::clamp(beta, 0.0, 20.0);
    synthesize();
}

void PfbFilterDesign::synthesize() {
    const int N = m_M * m_K;
    const double center = (N - 1) / 2.0;
    m_taps.assign(N, 0.0);
    double sum = 0.0;
    for (int n = 0; n < N; ++n) {
        // Ideal lowpass cutoff at Fs/(2*M): 2*fc = 1/M normalized to Fs.
        const double u = (n - center) / m_M;
        m_taps[n] = kaiserWindowAt(n, N, m_beta) * (1.0 / m_M) * sincP(u);
        sum += m_taps[n];
    }
    // Normalize DC gain to exactly 1 (H(0) = 1).
    if (sum != 0.0) {
        for (double &v : m_taps)
            v /= sum;
    }
}

double PfbFilterDesign::responseAt(double x) const {
    // DTFT magnitude of the real taps: H(x) = sum_n h[n] * exp(-j*2*pi*(x/M)*n).
    // The taps are symmetric about (N-1)/2 (linear phase), so the mirrored
    // terms of sum h[n]*cos(w*(n-center)) are equal: pair them and evaluate a
    // half-length cosine sum instead of a full complex sum (~4x fewer trigs).
    const int N = static_cast<int>(m_taps.size());
    const double w = 2.0 * M_PI * x / m_M; // rad/sample
    const double center = (N - 1) / 2.0;
    double acc = 0.0;
    const int half = N / 2;
    for (int i = 0; i < half; ++i)
        acc += 2.0 * m_taps[i] * std::cos(w * (i - center));
    if (N % 2 != 0)
        acc += m_taps[half]; // center tap of an odd-length filter, cos(0) = 1
    return std::abs(acc);
}

PfbFilterMetrics computePfbMetrics(const PfbFilterDesign &design) {
    PfbFilterMetrics m;
    m.total_taps = design.tapCount();
    m.edge_loss_db = toDb(design.responseAt(0.5));
    m.adjacent_rejection_db = toDb(design.responseAt(1.0));

    // Far-adjacent floor: worst response over x in [1.0, 1.5].
    double floor_db = -1e300;
    for (int i = 0; i <= 50; ++i) {
        floor_db = std::max(floor_db, toDb(design.responseAt(1.0 + 0.01 * i)));
    }
    m.far_floor_db = floor_db;

    // -3 dB half width: first crossing scanning outward from DC. The response
    // is monotonically decreasing through the main lobe, so the first sample
    // at or below -3 dB is the crossing.
    m.passband_halfwidth_ch = 1.0; // degenerate fallback (not reached for sane K)
    for (int i = 1; i <= 500; ++i) {
        const double x = 0.002 * i;
        if (toDb(design.responseAt(x)) <= -3.0) {
            m.passband_halfwidth_ch = x;
            break;
        }
    }

    // Flat-noise tilt: normalized noise bandwidth in dB (~ -0.6 dB for the corrected model).
    m.flat_noise_tilt_db = 10.0 * std::log10(std::max(design.normalizedNoiseBandwidth(), 1e-300));
    return m;
}

double PfbFilterDesign::normalizedNoiseBandwidth() const {
    // int_{-1}^{1} |H(x)|^2 dx, the channel-width-normalized noise bandwidth. H^2 is a
    // smooth bell, so 400 trapezoid steps are ample.
    const int steps = 400;
    double acc = 0.0;
    for (int i = 0; i <= steps; ++i) {
        const double h = responseAt(-1.0 + 2.0 * i / steps);
        acc += h * h;
    }
    return acc * 2.0 / steps;
}

RejectionStatus compareRejection(double rejection_db, double target_db) {
    const double achieved = -rejection_db; // positive magnitude
    if (achieved >= target_db)
        return RejectionStatus::Meets;
    if (achieved >= target_db - 10.0)
        return RejectionStatus::Within10Db;
    return RejectionStatus::Misses;
}

PfbRecommendation pfbRecommend(const PfbFilterDesign &design, const PfbFilterMetrics &metrics) {
    // Verdict metric is |H(1)| (metrics.adjacent_rejection_db). Only report a change that lowers
    // it.
    PfbRecommendation rec;
    const int M = design.channelCount();
    const int K = design.tapsPerBranch();
    const double current_db = metrics.adjacent_rejection_db;

    // Beta: the grid value that maximises |H(1)| at this K, when it beats the current beta.
    double best_beta = design.beta();
    double best_db = current_db;
    for (int b = 0; b <= 20; ++b) {
        const double db = toDb(PfbFilterDesign(M, K, b).responseAt(1.0));
        if (db < best_db) {
            best_db = db;
            best_beta = b;
        }
    }
    if (best_beta != design.beta())
        rec.beta = best_beta;

    // K: one step more, only when below the slider maximum and it lowers |H(1)|.
    if (K < 64 && toDb(PfbFilterDesign(M, K + 1, design.beta()).responseAt(1.0)) < current_db)
        rec.taps_per_branch = K + 1;

    return rec;
}

std::string pfbGuidanceText(const PfbFilterDesign &design, const PfbFilterMetrics &metrics,
                            double target_db, const PfbRecommendation &rec) {
    if (compareRejection(metrics.adjacent_rejection_db, target_db) == RejectionStatus::Meets)
        return {};
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Adjacent rejection %.1f dB is short of the %.0f dB target.",
                  -metrics.adjacent_rejection_db, target_db);
    std::string text = buf;
    if (rec.taps_per_branch.has_value()) {
        std::snprintf(buf, sizeof(buf), " Raise K (%d -> %d) to narrow the transition band.",
                      design.tapsPerBranch(), *rec.taps_per_branch);
        text += buf;
    }
    if (rec.beta.has_value()) {
        std::snprintf(buf, sizeof(buf),
                      " Set beta (%.1f -> %.1f): it gives the best adjacent rejection at K=%d.",
                      design.beta(), *rec.beta, design.tapsPerBranch());
        text += buf;
    }
    if (!rec.taps_per_branch.has_value() && !rec.beta.has_value())
        text +=
            design.tapsPerBranch() >= 64
                ? " K is at its slider maximum (64), and no other beta improves adjacent rejection."
                : " No larger K or other beta improves adjacent rejection at this M.";
    return text;
}
