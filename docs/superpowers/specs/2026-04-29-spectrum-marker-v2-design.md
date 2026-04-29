# Spectrum Marker Update — Live Tracking & Drag

## Goal

Enable the spectrum marker to automatically track a moving tone and be draggable via click-and-hold on the plot.

## Changes from Previous Design

The existing marker (v1) stores a `selected_peak_idx` pointing into a snapshot of detected peaks. This update replaces index-based tracking with **frequency-proximity tracking** and adds **drag-to-snap** interaction.

## Architecture

Engine ↔ Widget boundary remains unchanged:
- **Engine** (`SpectrumAnalyzerEngine`): owns `findPeaks()` — no changes needed.
- **Widget** (`SpectrumAnalyzerWidget`): owns marker state, UI, and interaction.

## Data Model

### `MarkerState` (revised)

```cpp
struct MarkerState {
    bool enabled = false;
    bool is_dragging = false;
    double target_freq_Hz = 0.0;   // frequency to track (peaks) or release position (drag)
    std::vector<Peak> peaks;       // latest detected peaks (refreshed every frame)
};
```

Removed: `selected_peak_idx`, `manual_bin`. Tracking is now purely frequency-based.

### `MarkerInfo` (unchanged)

```cpp
struct MarkerInfo {
    int idx = 0;
    double freq_Hz = 0.0;
    double power_dBm = -174.0;
};
```

## Behavior

### Peak Tracking (every frame, when enabled and not dragging)

1. Re-run `findPeaks(display_dBm, freq_axis, 8)` → `peaks`
2. If `peaks` is empty, fall back to nearest bin to `target_freq_Hz`
3. Otherwise, find peak with minimum `|peak.freq_Hz - target_freq_Hz|`
4. Render marker at that peak; update `target_freq_Hz = peak.freq_Hz`

This makes the marker "stick" to a tone as its frequency drifts.

### Drag Interaction

1. **Mouse down** inside plot bounds (`ImPlot::IsPlotHovered() && ImGui::IsMouseDown(0)`) → `is_dragging = true`
2. **While dragging** → read mouse X from `ImPlot::GetPlotMousePos()`, set `target_freq_Hz` to mouse X. Render marker at nearest bin to mouse X.
3. **Mouse up** (`!ImGui::IsMouseDown(0)`) → `is_dragging = false`. Re-detect peaks, snap to nearest peak, update `target_freq_Hz`.

### Enable Checkbox

When checked:
- Initialize `target_freq_Hz` to center frequency (`(start_freq + stop_freq) / 2`)
- Re-detect peaks, snap to nearest peak

### Left / Right Buttons

- `<` / `>` buttons move `target_freq_Hz` by one bin width
- After nudging, re-detect peaks and snap to nearest peak
- These buttons remain useful for fine adjustment without dragging

## UI Layout (unchanged)

Below the spectrum plot:
- "Enable Marker" checkbox
- "Snap to Peak" / "Next Peak" buttons (optional — tracking now automatic)
- `<` / `>` buttons
- Readout: `Marker: %.2f MHz, %.2f dBm`

## Visual

- Orange downward triangle (`ImPlotMarker_Down`) at marker position
- During drag, marker follows mouse horizontally

## Files to Modify

- `spectrum_analyzer/include/spectrum_analyzer_widget.h` — update `MarkerState`
- `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` — update `resolveMarker`, drag logic

## Testing

- Existing `findPeaks` tests remain valid (engine unchanged)
- Manual verification: add tone, move frequency, confirm marker follows
- Manual verification: drag marker, release, confirm snaps to nearest peak
