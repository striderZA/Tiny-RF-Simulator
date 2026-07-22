# Spectrum Analyzer — Trace Modes Design

**Date:** 2026-07-22
**Status:** Draft — pending user review
**Scope:** Trace modes (Max Hold, Min Hold, Video Average) for the spectrum analyzer. Detectors are deferred to a follow-up that introduces a display-points/decimation step (without decimation, all detectors reduce to identity).

## 1. Background

The current spectrum analyzer displays each FFT frame directly. Users cannot accumulate information across frames, which makes it hard to observe intermittent peaks, locate the noise floor reliably, or smooth a noisy display.

Real spectrum analyzers offer trace modes that manage history across frames. This design adds the four trace modes that matter for daily use: Clear/Write, Max Hold, Min Hold, and Video Average.

## 2. Goals

- Add four trace modes: ClearWrite (default), MaxHold, MinHold, VideoAverage.
- Expose trace-mode picker and avg-count control in the widget UI.
- Keep history per visible trace (keyed by `const Spectrum*`) so switching probe visibility preserves per-trace state.
- Follow the existing codebase pattern: engine owns state and DSP, widget owns UI. No new intermediate processing layer.
- Keep changes testable with unit tests that exercise each mode's temporal behavior.

## 3. Non-Goals (deferred)

- **Detectors (Peak / Sample / Average / RMS).** In this simulator, one FFT bin maps to one display point, so detection windows are 1 bin wide and all detectors reduce to identity. Detectors become meaningful only when paired with a display-points/decimation control. Deferred to a follow-up.
- **Per-trace detector/mode pickers.** One global trace-mode setting applies to all visible traces. Per-trace pickers are part of a future multi-trace UI (Trace 1/2/3/4) and can be layered on top of this design without internal changes.
- **View / Store (reference trace).** Requires a dedicated trace slot. Part of the multi-trace follow-up.
- **RMS trace mode.** RMS across frames per bin is meaningful but similar enough to Average that it's not worth the surface area here. Can be added later as a fifth mode.

## 4. Architecture

### 4.1 Approach: Extend the engine

The engine (`SpectrumAnalyzerEngine`) already owns mutable state (RBW cache, noise jitter RNG). Adding trace-mode state to it is consistent with this existing pattern. No new `TraceProcessor` class or intermediate layer is introduced — the codebase's universal pattern is `Engine → Widget` and this design preserves it.

### 4.2 Pipeline

New pipeline: `integratePowerPerBin → applyRBW → W_to_dBm → jitter → applyVBW → applyTraceMode → return`.

Trace mode is applied last, after VBW. Changing trace mode does not invalidate the RBW cache — only the post-cache steps (jitter, VBW, trace mode) re-run. This is correct: Max Hold of a re-jittered frame matches expected SA behavior.

`renderCombinedSpectrum` (used for marker snapping and noise readouts) is **unchanged** — it does not apply trace mode. Markers operate on the raw combined signal.

## 5. Data Model

### 5.1 Enum

```cpp
enum class TraceMode { ClearWrite, MaxHold, MinHold, VideoAverage };
```

### 5.2 New engine API

```cpp
void setTraceMode(TraceMode m);
void setVideoAvgCount(int n);

TraceMode traceMode() const;
int videoAvgCount() const;

void resetTraceHistory();  // clears all per-trace history buffers
void pruneHistory(const std::vector<const Spectrum*>& active_keys);  // removes stale entries
```

### 5.3 Per-trace history buffers

```cpp
mutable std::unordered_map<const Spectrum*, std::vector<double>> m_max_hold;
mutable std::unordered_map<const Spectrum*, std::vector<double>> m_min_hold;
mutable std::unordered_map<const Spectrum*, std::vector<double>> m_video_avg;
```

Keyed by `const Spectrum*` — same key as the existing RBW cache.

### 5.4 Defaults

- `TraceMode`: `ClearWrite`
- `videoAvgCount`: `10`

## 6. Semantics

### 6.1 Trace modes

| Mode | Behavior |
|---|---|
| `ClearWrite` | Each frame overwrites the trace. No history. Current behavior. |
| `MaxHold` | Per-bin `max(current_history, new_frame)`. Persisted until reset. |
| `MinHold` | Per-bin `min(current_history, new_frame)`. Persisted until reset. |
| `VideoAverage` | EWMA: `trace[i] = α·frame[i] + (1-α)·trace[i-1]`, where `α = 2/(count+1)`. |

### 6.2 Mode switch

Changing trace mode resets that trace's history (fresh start in new mode). Implementation: the engine clears all three history maps on `setTraceMode()`.

### 6.3 Size mismatch

If a trace's history buffer size doesn't match the incoming frame (first frame, or spectrum changed size), history is reset to the current frame.

### 6.4 Lifecycle / pruning

When a `Spectrum*` key no longer appears in active nodes, its history entries are pruned. `pruneHistory(active_keys)` is a **public** engine method called by the widget once per frame with the set of visible `Spectrum*` pointers. It erases entries whose key is not in the active set.

## 7. Engine Implementation

New private method:

```cpp
std::vector<double> applyTraceMode(const Spectrum& spec,
                                    const std::vector<double>& after_vbw) const;
```

```cpp
std::vector<double> SpectrumAnalyzerEngine::applyTraceMode(
    const Spectrum& spec, const std::vector<double>& after_vbw) const
{
    if (m_trace_mode == TraceMode::ClearWrite) {
        return after_vbw;
    }

    std::unordered_map<const Spectrum*, std::vector<double>>* history = nullptr;
    if (m_trace_mode == TraceMode::MaxHold) history = &m_max_hold;
    else if (m_trace_mode == TraceMode::MinHold) history = &m_min_hold;
    else history = &m_video_avg;
    auto& h = (*history)[&spec];

    // Size mismatch → reset
    if (h.size() != after_vbw.size()) {
        h = after_vbw;
        return after_vbw;
    }

    if (m_trace_mode == TraceMode::MaxHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::max(h[i], after_vbw[i]);
    } else if (m_trace_mode == TraceMode::MinHold) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = std::min(h[i], after_vbw[i]);
    } else { // VideoAverage
        double alpha = 2.0 / (m_video_avg_count + 1);
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = alpha * after_vbw[i] + (1.0 - alpha) * h[i];
    }
    return h;
}
```

### 7.1 Pruning

```cpp
void SpectrumAnalyzerEngine::pruneHistory(
    const std::vector<const Spectrum*>& active_keys) const
{
    std::unordered_set<const Spectrum*> active(active_keys.begin(), active_keys.end());
    for (auto* map : {&m_max_hold, &m_min_hold, &m_video_avg}) {
        for (auto it = map->begin(); it != map->end(); ) {
            if (active.find(it->first) == active.end()) {
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
}
```

## 8. Widget UI

### 8.1 New controls

Inserted after the RBW control and before the Ref Level control in the widget's control panel:

| Control | Widget | Behavior |
|---|---|---|
| Trace Mode | `ImGui::Combo` with labels `Clear/Write`, `Max Hold`, `Min Hold`, `Video Avg` | Calls `m_engine.setTraceMode(...)`. |
| Avg Count | `ImGui::SliderInt` or `ImGui::DragInt`, range 2–100, step 1 | Visible and enabled only when mode == `VideoAverage`. Grayed out otherwise. Calls `m_engine.setVideoAvgCount(...)`. |
| Reset Hold | `ImGui::Button` | Visible only when mode == `MaxHold` or `MinHold`. Calls `m_engine.resetTraceHistory()`. |

### 8.2 Status line

Below the plot, extend the existing status area:

```
Trace: Max Hold | Peak: 1.23 MHz, -12.4 dBm
```

The peak readout is the maximum value in the `combined_dBm` vector (the widget already computes this for marker snapping). Trace mode applies only to per-trace `renderSpectrum` for display. `renderCombinedSpectrum` remains raw (no trace mode), so markers snap to actual signal peaks rather than held/averaged peaks.

## 9. Testing

Adding to `tests/test_main.cpp` in the existing `Spectrum analyzer` test section:

| Test | Description |
|---|---|
| ClearWrite identity | Feed 3 frames → output equals last frame |
| MaxHold accumulates | Feed frame A, then frame B (higher per bin) → output is per-bin max(A, B) |
| MinHold accumulates | Feed frame A, then frame B (lower per bin) → output is per-bin min(A, B) |
| VideoAverage EWMA convergence | Feed constant frame → output converges to that frame |
| VideoAverage step response | Feed step change → output exponentially approaches new value |
| Mode switch resets | Set MaxHold, feed frames, switch to ClearWrite → next output equals current frame (no stale history) |
| Size mismatch reset | Feed frame of size N, then frame of size M → history resets to new frame |
| History prune | Add node, feed frames (creates history), remove node, render with empty active set → all history maps empty |

## 10. Files Changed

| File | Change |
|---|---|
| `spectrum_analyzer/include/spectrum_analyzer_engine.h` | Add `TraceMode` enum, new setters/getters, history buffers, `applyTraceMode`, `pruneHistory`, `resetTraceHistory` |
| `spectrum_analyzer/src/spectrum_analyzer_engine.cpp` | Implement `applyTraceMode`, `pruneHistory`, `resetTraceHistory`. Modify `renderSpectrum` pipeline. |
| `spectrum_analyzer/src/spectrum_analyzer_widget.cpp` | Add trace mode combo, avg count slider, reset button, status line extension. |
| `tests/test_main.cpp` | Add 8 trace-mode unit tests |

## 11. Risks

- **Performance:** History buffers add per-trace memory (one `vector<double>` per mode per visible trace). Negligible for typical graph sizes. The `unordered_map` lookup is O(1) amortized.
- **Thread safety:** Engine is accessed from the render thread only. No threading concerns.
- **History leak on node removal:** Mitigated by `pruneHistory()` called every frame.
- **EWMA divergence with `avgCount = 1`:** `α = 2/2 = 1` → no averaging. Slider floor is 2 to prevent this.
