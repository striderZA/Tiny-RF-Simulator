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
    const double norm = x / m_M; // frequency in cycles/sample
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < static_cast<int>(m_taps.size()); ++n) {
        const double ph = 2.0 * M_PI * norm * n;
        re += m_taps[n] * std::cos(ph);
        im += m_taps[n] * std::sin(ph);
    }
    return std::hypot(re, im);
}
