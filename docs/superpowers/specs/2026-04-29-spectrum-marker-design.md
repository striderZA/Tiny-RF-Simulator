# Spectrum Marker Widget Design

**Date:** 2026-04-29
**Scope:** Add an interactive spectrum marker to the spectrum analyzer widget

## Goal

Enable users to place a visual marker on the spectrum plot, snap it to detected peaks, and read off the exact frequency and power at the marker position.

## Architecture

Follow the existing `engine` + `widget` separation:

- `SpectrumAnalyzerEngine` owns peak detection (pure DSP, no ImGui)
- `SpectrumAnalyzerWidget` owns marker UI state and rendering

## Engine Changes

### `findPeaks()`

```cpp
std::vector<Peak> findPeaks(const std::vector<double>& power_dBm,
                            const std::vector<double>& freq_axis,
                            int max_count = 8) const;
```

- Detects local maxima where `power_dBm[i] > power_dBm[i-1] && power_dBm[i] > power_dBm[i+1]`
- Skips first and last bins (endpoints cannot be peaks)
- Sorts peaks by `power_dBm` descending
- Returns up to `max_count` peaks
- Each `Peak` contains `index`, `freq_Hz`, `power_dBm`

## Widget Changes

### `MarkerState` (private struct)

```cpp
struct MarkerState {
    bool enabled = false;
    int selected_peak_idx = -1;   // -1 = manual position, >=0 = snapped to peak list
    int manual_bin = 0;
    std::vector<Peak> peaks;      // cached from last "Snap to Peak"
};
```

### UI Controls (above the plot, below the existing controls)

1. **Checkbox:** "Enable Marker" — toggles `enabled`
2. When enabled:
   - **"Snap to Peak"** button — calls `findPeaks()`, caches list, sets `selected_peak_idx = 0` (highest)
   - **"Next Peak"** button — `selected_peak_idx = (selected_peak_idx + 1) % peaks.size()`
   - **Left / Right arrow buttons** (`<` / `>`) — decrement/increment `manual_bin`, clamp to `[0, N-1]`, set `selected_peak_idx = -1`
   - **Text display:** `Marker: FREQ MHz, POWER dBm`

### Marker Position Logic

On every `draw()`:
- If `enabled == false`: skip marker rendering
- If `selected_peak_idx >= 0` and `peaks` not empty: position = `peaks[selected_peak_idx]`
- Else (manual): position = `freq_axis[manual_bin]`, `display_dBm[manual_bin]`

### Plot Visual

- Upside-down triangle rendered at marker `(freq, power)` using `ImPlot::PlotScatter` with marker style `ImPlotMarker_Down` or `ImPlotMarker_TriangleDown`
- Triangle colour: distinct from the spectrum line (e.g. orange/red)

## Data Flow

```
User clicks "Snap to Peak"
  -> Widget calls engine.findPeaks(display_dBm, freq_axis, 8)
  -> Peaks cached in MarkerState
  -> selected_peak_idx = 0
  -> Marker position updated from peaks[0]
  -> Triangle + text re-rendered

User clicks "Next Peak"
  -> selected_peak_idx cycles
  -> Marker position updated

User clicks Left/Right
  -> manual_bin adjusted, selected_peak_idx = -1
  -> Marker position updated from data arrays
```

Peak list is recomputed only on "Snap to Peak", not every frame.

## Edge Cases

- No peaks found (flat spectrum): `peaks` empty, `selected_peak_idx` stays -1, marker remains at manual position
- `manual_bin` clamped to `[0, display_dBm.size() - 1]`
- Marker disabled by default (no visual clutter)

## Testing

- Unit test `findPeaks` with synthetic spectra:
  - Single tone → 1 peak at correct index
  - Multiple tones → peaks sorted by power
  - Flat noise → empty peak list
  - 10 tones with max_count=8 → returns exactly 8 highest

## Files Modified

- `spectrum_analyzer/include/spectrum_analyzer_engine.h` — add `findPeaks` declaration
- `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` — implement `findPeaks`
- `spectrum_analyzer/include/spectrum_analyzer_widget.h` — add `MarkerState`, `drawMarker`, `m_marker`
- `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` — add marker UI and rendering
- `tests/test_main.cpp` — add `findPeaks` tests
