#pragma once
#include "spectrum_analyzer_trace.h"
#include <array>
#include <optional>
#include <utility>
#include <vector>

// IM3 (third-order intermodulation) measurement view state for the Spectrum Analyzer. UI-free:
// it owns the show/hide flag, the marker-source switch, and the search rules, while the widget
// owns presentation. The measurement reads the same jitter-free readout trace as Peak, the
// markers, and the harmonic measurement, so it follows the measurement source, trace
// visibility, and the hold modes.
//
// The two fundamentals are the strongest two tone-like peaks of the trace that stand clear of
// its noise floor (a real-domain trace's +-f image is treated as one tone), or the two marker
// frequencies the user supplied. They are ordered by frequency: f1 is the lower, f2 the higher.
// The 2*f1-f2 and 2*f2-f1 products are searched within one resolution bandwidth of their
// expected frequency. A product outside the swept span, one the RBW or the bin spacing cannot
// separate from its fundamental, and one hidden in the noise floor are reported, never guessed.
// The reported TOI is a spectrum-derived output-intercept estimate, not a receiver IIP3 test.
class Im3Model {
  public:
    static constexpr double kPeakClearance_dB = spectrum_trace::kPeakClearance_dB;

    enum class Status {
        Measured,       // both tones found, or both products measured (per field)
        NeedTwoTones,   // the trace has fewer than two clear tones, or the markers are unusable
        TonesOutOfSpan, // a supplied marker tone is outside the swept span
        OutOfSpan,      // the product frequency is outside the swept span
        Unresolved,     // the RBW or bin spacing cannot separate the product from its tone
        NotFound,       // inside the span, but no product peak stands clear of the noise floor
    };

    struct Tone {
        double frequency_Hz = 0.0;
        double amplitude_dBm = 0.0;
    };

    struct Product {
        // false: the low-side product 2*f1 - f2, nearest f1. true: 2*f2 - f1, nearest f2.
        bool high_side = false;
        Status status = Status::NeedTwoTones;
        double expected_Hz = 0.0;  // 2*f1 - f2 or 2*f2 - f1
        double frequency_Hz = 0.0; // measured peak, or expected when not measured
        double amplitude_dBm = 0.0;
        double relative_dBc = 0.0;  // product level minus its nearest fundamental's level
        double intercept_dBm = 0.0; // per-product output-intercept estimate (TOI)
        // Unresolved only: true when the RBW is the limit, false when the bin spacing is.
        bool rbw_limited = false;
    };

    struct Result {
        Status tone_status = Status::NeedTwoTones;
        std::array<Tone, 2> tones{};       // [0] = f1 (lower), [1] = f2 (higher)
        std::array<Product, 2> products{}; // [0] = low side, [1] = high side
        bool toi_valid = false;
        double toi_dBm = 0.0; // mean of the measured products' intercept estimates
    };

    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    // True when the widget supplies two marker frequencies as the fundamentals.
    bool useMarkers() const { return m_use_markers; }
    void setUseMarkers(bool use_markers) { m_use_markers = use_markers; }

    // Replaces the stored result from `trace_dBm` on `freq_axis`. `span_start_Hz` and
    // `span_stop_Hz` are the displayed span; `rbw_Hz` sets the product search half-width and
    // the resolution rule. When `tones_Hz` is set, those two user-placed marker frequencies
    // are taken as the fundamentals; otherwise the two strongest clear tones are.
    void measure(const std::vector<double> &freq_axis, const std::vector<double> &trace_dBm,
                 double span_start_Hz, double span_stop_Hz, double rbw_Hz,
                 std::optional<std::pair<double, double>> tones_Hz = std::nullopt);
    const Result &result() const { return m_result; }

  private:
    Result m_result;
    bool m_visible = false;
    bool m_use_markers = false;
};
