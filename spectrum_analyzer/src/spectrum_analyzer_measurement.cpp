#include "spectrum_analyzer_measurement.h"

MeasurementSelection::Resolution
MeasurementSelection::resolve(const std::vector<MeasurementProbeId> &ids,
                              const std::vector<bool> &visible) const {
    Resolution result;
    if (!m_probe.has_value()) {
        return result;
    }

    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] != *m_probe) {
            continue;
        }
        result.requested_index = static_cast<int>(i);
        const bool shown = visible.empty() || (i < visible.size() && visible[i]);
        if (!shown) {
            result.fallback = Fallback::Hidden;
            return result;
        }
        result.probe_index = static_cast<int>(i);
        return result;
    }

    // The chosen probe is no longer probed. It may reappear under the same identity, so the
    // selection is kept rather than cleared.
    result.fallback = Fallback::Removed;
    return result;
}
