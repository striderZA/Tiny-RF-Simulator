#pragma once

#include <optional>
#include <string>
#include <vector>

// Real polyphase prototype for the PFB channelizer, shared by the engine
// (channel weights) and the Filter Calculator tool (metrics/plots) so the two
// can never drift. Design: windowed-sinc lowpass with cutoff at the
// half-channel point Fs/(2*M), Kaiser window of length N = K*M, DC gain
// normalized so H(0) = 1 exactly.
class PfbFilterDesign {
  public:
    PfbFilterDesign(int M = 32, int K = 8, double beta = 8.0);
    // |H(x)| at normalized offset x in channel-width units (x = offset/(Fs/M)).
    // Negative x is allowed; the response is symmetric about 0. O(K*M) per call.
    double responseAt(double x) const;
    // Continuous noise bandwidth in channel-width units: int_{-1}^{1} |H(x)|^2 dx.
    // Multiply by the channel width to get the channel ENBW in Hz.
    double normalizedNoiseBandwidth() const;
    const std::vector<double> &taps() const { return m_taps; }
    int tapCount() const { return static_cast<int>(m_taps.size()); }
    int channelCount() const { return m_M; }
    int tapsPerBranch() const { return m_K; }
    double beta() const { return m_beta; }

  private:
    void synthesize();
    int m_M;
    int m_K;
    double m_beta;
    std::vector<double> m_taps;
};

// Achieved filter metrics, computed from the same core the engine uses.
struct PfbFilterMetrics {
    double passband_halfwidth_ch = 0.0; // -3 dB half width, in channel units
    double edge_loss_db = 0.0;          // |H(0.5)| in dB (~ -6 for all designs)
    double adjacent_rejection_db = 0.0; // |H(1.0)| in dB; compared to the target
    double far_floor_db = 0.0;          // max |H(x)| over x in [1.0, 1.5], dB
    int total_taps = 0;                 // N = M*K
    double flat_noise_tilt_db = 0.0;    // 10*log10( int_{-1..1} H(x)^2 dx )
};

PfbFilterMetrics computePfbMetrics(const PfbFilterDesign &design);

// rejection_db is a negative dB magnitude (e.g. -83.2). target_db is the
// positive required suppression (e.g. 80). Meets => |rejection| >= target_db;
// Within10Db => |rejection| >= target_db - 10; Misses otherwise.
enum class RejectionStatus { Meets, Within10Db, Misses };
RejectionStatus compareRejection(double rejection_db, double target_db);

// Suggested single-parameter change for a missed rejection target. At most one field
// is set. Each candidate is evaluated with the other parameter unchanged, so the
// suggested setting is exactly the one whose |H(1)| was checked. A candidate is
// shown only when it lowers |H(1)| (the verdict metric) below the current design.
struct PfbRecommendation {
    bool searched = false;              // false: M*K above kPfbRecommendMaxTaps, no search run
    std::optional<int> taps_per_branch; // K+1 or 64 (slider max), at the current beta
    std::optional<double> beta;         // best of beta 0, 4, ..., 20, at the current K
};

// Tap budget (M*K) above which the search is skipped. Measured at the budget: the search
// adds 35-95 ms per refresh (1.9x-4.1x the base refresh, M=2048 K=4 worst). Above the
// budget no search runs (0 ms), so large designs keep the pre-#289 refresh cost.
inline constexpr int kPfbRecommendMaxTaps = 8192;

// Evaluates the candidate single changes above. Synthesizes one prototype per candidate,
// so cache the result per design rather than calling it every frame.
PfbRecommendation pfbRecommend(const PfbFilterDesign &design, const PfbFilterMetrics &metrics);

// Short manual-guidance string. Empty when the rejection target is met.
std::string pfbGuidanceText(const PfbFilterDesign &design, const PfbFilterMetrics &metrics,
                            double target_db, const PfbRecommendation &rec);
