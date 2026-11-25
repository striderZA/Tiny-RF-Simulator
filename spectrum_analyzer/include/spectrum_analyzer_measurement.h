#pragma once
#include <cstddef>
#include <optional>
#include <vector>

// Identity of one probed trace. The node pointer and output index survive probe-list
// reordering, so a selection keeps naming the same trace when another probe is added or
// removed. This class is UI-free: the widget owns the labels, the model owns the selection
// rules and the fallback when the chosen probe cannot be measured.
struct MeasurementProbeId {
    const void *node = nullptr;
    int output_index = 0;

    bool operator==(const MeasurementProbeId &other) const {
        return node == other.node && output_index == other.output_index;
    }
    bool operator!=(const MeasurementProbeId &other) const { return !(*this == other); }
};

// Measurement source for the Spectrum Analyzer readouts. The default is every visible trace
// (the historic combined readout); picking a probe scopes the readouts and peak searches to
// that one trace. A chosen probe that is hidden or gone falls back to the visible traces and
// reports why, instead of silently measuring another trace.
class MeasurementSelection {
  public:
    static constexpr int kAllVisible = -1;

    enum class Fallback {
        None,    // the selection resolves as asked
        Hidden,  // the chosen probe exists but its trace is hidden
        Removed, // the chosen probe is no longer in the probe list
    };

    struct Resolution {
        int probe_index = kAllVisible;     // effective slot, or kAllVisible for all visible
        int requested_index = kAllVisible; // slot of the chosen probe, or kAllVisible if gone
        Fallback fallback = Fallback::None;

        bool allVisible() const { return probe_index == kAllVisible; }
        bool fallbackActive() const { return fallback != Fallback::None; }
    };

    bool allVisible() const { return !m_probe.has_value(); }
    std::optional<MeasurementProbeId> probe() const { return m_probe; }

    void selectAllVisible() { m_probe.reset(); }
    void selectProbe(MeasurementProbeId probe) { m_probe = probe; }

    // `ids` lists the current probes in display order; `visible` marks the shown traces,
    // parallel to `ids` (an empty vector means all visible).
    Resolution resolve(const std::vector<MeasurementProbeId> &ids,
                       const std::vector<bool> &visible) const;

  private:
    std::optional<MeasurementProbeId> m_probe;
};
