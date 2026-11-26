#pragma once
#include "spectrum_analyzer_trace.h"
#include <array>
#include <optional>
#include <vector>

// Harmonic measurement view state for the Spectrum Analyzer. UI-free: it owns the show/hide
// flag, the fundamental-source switch, and the search rules, while the widget owns
// presentation. The measurement reads the same jitter-free readout trace as Peak and the
// markers, so it follows the measurement source, trace visibility, and the hold modes.
//
// The fundamental is the strongest tone-like peak of the trace that stands clear of its noise
// floor, or the frequency of the marker the user placed on it. The search is sign-agnostic,
// so a one-sided complex-baseband trace measures a negative-frequency tone; on a mirrored
// real-domain trace an exact +-f tie prefers the positive image. The 2nd and 3rd harmonics are
// searched within one resolution bandwidth of 2*f0 and 3*f0. A harmonic outside the swept span
// is reported as out of span rather than extrapolated, and one that the RBW or the bin spacing
// cannot separate from its neighbour is reported unresolved.
class HarmonicModel {
  public:
    static constexpr int kMaxOrder = 3; // reports the 2nd and 3rd
    // A bin must rise this far above the trace's median level to count as a tone. The
    // trace is jitter-free, so this only rejects the noise floor and its rounding ripple.
    static constexpr double kPeakClearance_dB = spectrum_trace::kPeakClearance_dB;

    enum class Status {
        Measured,             // a peak was found and measured
        NoFundamental,        // the trace has no clear tone-like peak
        FundamentalOutOfSpan, // the supplied marker frequency is outside the swept span
        OutOfSpan,            // the harmonic frequency is outside the swept span
        Unresolved,           // the RBW or bin spacing is too wide to separate adjacent orders
        NotFound,             // inside the span, but no peak stands clear of the noise floor
    };

    struct Entry {
        int order = 2;
        Status status = Status::NotFound;
        double expected_Hz = 0.0;  // order * fundamental
        double frequency_Hz = 0.0; // measured peak, or expected when not measured
        double amplitude_dBm = 0.0;
        double relative_dBc = 0.0; // harmonic level minus the fundamental level
        // Unresolved only: true when the RBW is the limit, false when the bin spacing is.
        bool rbw_limited = false;
    };

    struct Result {
        Status fundamental_status = Status::NoFundamental;
        double fundamental_Hz = 0.0;
        double fundamental_dBm = 0.0;
        std::array<Entry, kMaxOrder - 1> harmonics{};
    };

    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    // True when the widget takes the selected marker's frequency as the fundamental.
    bool useMarker() const { return m_use_marker; }
    void setUseMarker(bool use_marker) { m_use_marker = use_marker; }

    // Replaces the stored result from `trace_dBm` on `freq_axis`. `span_start_Hz` and
    // `span_stop_Hz` are the displayed span; `rbw_Hz` sets the harmonic search half-width.
    // When `fundamental_Hz` is set, that frequency (the marker the user placed) is taken as
    // the fundamental; otherwise the strongest clear tone is.
    void measure(const std::vector<double> &freq_axis, const std::vector<double> &trace_dBm,
                 double span_start_Hz, double span_stop_Hz, double rbw_Hz,
                 std::optional<double> fundamental_Hz = std::nullopt);
    const Result &result() const { return m_result; }

  private:
    Result m_result;
    bool m_visible = false;
    bool m_use_marker = false;
};
