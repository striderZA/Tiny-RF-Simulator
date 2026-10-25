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
// Verdict basis: max |H(x)| over x in [1.0, 1.5], dB. Sidelobes are about 1/K wide in channel
// units, so the scan step shrinks with K (at least four samples per lobe). A sampled local
// maximum can sit up to ~0.7 dB below its true peak, so every local maximum within 1 dB of the
// best sample is refined by golden-section search; a lower lobe cannot overtake it otherwise.
double farFloorDb(const PfbFilterDesign &design) {
    const double kStep = std::min(0.01, 0.25 / design.tapsPerBranch());
    const int kSteps = static_cast<int>(std::ceil(0.5 / kStep));
    const auto xAt = [kStep](int i) { return std::min(1.5, 1.0 + kStep * i); };
    std::vector<double> samples(kSteps + 1);
    double best_db = -1e300;
    for (int i = 0; i <= kSteps; ++i) {
        samples[i] = toDb(design.responseAt(xAt(i)));
        best_db = std::max(best_db, samples[i]);
    }
    constexpr double kInvPhi = 0.6180339887498949;
    double peak_db = best_db;
    for (int i = 0; i <= kSteps; ++i) {
        const bool local_max = (i == 0 || samples[i] >= samples[i - 1]) &&
                               (i == kSteps || samples[i] >= samples[i + 1]);
        if (!local_max || samples[i] < best_db - 1.0)
            continue;
        double lo = xAt(std::max(0, i - 1));
        double hi = xAt(std::min(kSteps, i + 1));
        double c = hi - kInvPhi * (hi - lo);
        double d = lo + kInvPhi * (hi - lo);
        double fc = toDb(design.responseAt(c));
        double fd = toDb(design.responseAt(d));
        for (int it = 0; it < 30; ++it) {
            if (fc > fd) {
                hi = d;
                d = c;
                fd = fc;
                c = hi - kInvPhi * (hi - lo);
                fc = toDb(design.responseAt(c));
            } else {
                lo = c;
                c = d;
                fc = fd;
                d = lo + kInvPhi * (hi - lo);
                fd = toDb(design.responseAt(d));
            }
        }
        peak_db = std::max({peak_db, fc, fd});
    }
    return peak_db;
}
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
    // half-length cosine sum instead of a full complex sum.
    // cos(w*(i-center)) is produced by rotating unit phasors instead of a
    // cos() call per tap. Four phasor chains advance by 4w per step so their
    // rotations overlap. Each chain is re-anchored from exact sin/cos every
    // kAnchor taps, which bounds the rounding drift of the recurrence.
    constexpr int kAnchor = 256; // multiple of 4
    const int N = static_cast<int>(m_taps.size());
    const double w = 2.0 * M_PI * x / m_M; // rad/sample
    const double center = (N - 1) / 2.0;
    const int half = N / 2;
    const double *t = m_taps.data();
    const double c4 = std::cos(4.0 * w);
    const double s4 = std::sin(4.0 * w);
    double sum = 0.0;
    for (int base = 0; base < half; base += kAnchor) {
        const int end = std::min(half, base + kAnchor);
        double c[4];
        double s[4];
        double part[4] = {0.0, 0.0, 0.0, 0.0};
        for (int j = 0; j < 4; ++j) {
            const double theta = w * (base + j - center);
            c[j] = std::cos(theta);
            s[j] = std::sin(theta);
        }
        int i = base;
        for (; i + 4 <= end; i += 4) {
            for (int j = 0; j < 4; ++j) {
                part[j] += t[i + j] * c[j];
                const double next_c = c[j] * c4 - s[j] * s4;
                s[j] = s[j] * c4 + c[j] * s4;
                c[j] = next_c;
            }
        }
        // Tail: fewer than 4 taps left in this block; c[j] is the phasor for i + j.
        for (int j = 0; i + j < end; ++j)
            part[j] += t[i + j] * c[j];
        sum += part[0] + part[1] + part[2] + part[3];
    }
    double acc = 2.0 * sum;
    if (N % 2 != 0)
        acc += t[half]; // center tap of an odd-length filter, cos(0) = 1
    return std::abs(acc);
}

PfbFilterMetrics computePfbMetrics(const PfbFilterDesign &design) {
    PfbFilterMetrics m;
    m.total_taps = design.tapCount();
    m.edge_loss_db = toDb(design.responseAt(0.5));
    m.adjacent_rejection_db = toDb(design.responseAt(1.0));

    // Verdict basis: the far floor over [1.0, 1.5], which includes H(1.0).
    m.far_floor_db = farFloorDb(design);

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
    PfbRecommendation rec;
    const int M = design.channelCount();
    const int K = design.tapsPerBranch();
    if (design.tapCount() > kPfbRecommendMaxTaps)
        return rec;
    rec.searched = true;

    // Verdict metric is the far floor over the alias band (metrics.far_floor_db). Each
    // candidate changes one parameter and keeps the other, so the shown setting is the one
    // that was evaluated.
    double best_db = metrics.far_floor_db;
    for (int b = 0; b <= 20; b += 4) {
        if (static_cast<double>(b) == design.beta())
            continue;
        const double db = farFloorDb(PfbFilterDesign(M, K, b));
        if (db < best_db) {
            best_db = db;
            rec.beta = b;
            rec.taps_per_branch.reset();
        }
    }
    for (int k : {K + 1, 64}) {
        if (k <= K || k > 64)
            continue;
        const double db = farFloorDb(PfbFilterDesign(M, k, design.beta()));
        if (db < best_db) {
            best_db = db;
            rec.taps_per_branch = k;
            rec.beta.reset();
        }
    }
    return rec;
}

std::string pfbGuidanceText(const PfbFilterDesign &design, const PfbFilterMetrics &metrics,
                            double target_db, const PfbRecommendation &rec) {
    if (compareRejection(metrics.far_floor_db, target_db) == RejectionStatus::Meets)
        return {};
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Worst-case rejection %.1f dB is short of the %.0f dB target.",
                  -metrics.far_floor_db, target_db);
    std::string text = buf;
    if (!rec.searched) {
        std::snprintf(buf, sizeof(buf),
                      " No suggestion: M*K = %d is above %d taps, where the search is skipped.",
                      design.tapCount(), kPfbRecommendMaxTaps);
        return text + buf;
    }
    if (rec.taps_per_branch.has_value()) {
        std::snprintf(buf, sizeof(buf),
                      " Set K (%d -> %d) at beta %.1f: this lowers worst-case rejection.",
                      design.tapsPerBranch(), *rec.taps_per_branch, design.beta());
        text += buf;
    } else if (rec.beta.has_value()) {
        std::snprintf(buf, sizeof(buf),
                      " Set beta (%.1f -> %.1f) at K=%d: this lowers worst-case rejection.",
                      design.beta(), *rec.beta, design.tapsPerBranch());
        text += buf;
    } else {
        std::snprintf(buf, sizeof(buf),
                      " None of the checked single changes improves it (K+1, K=64 if below, "
                      "or beta 0, 4, ..., 20 at K=%d).",
                      design.tapsPerBranch());
        text += buf;
    }
    return text;
}
