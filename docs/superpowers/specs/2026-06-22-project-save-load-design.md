# Project Save/Load Design

**Date:** 2026-06-22
**Status:** Design

## Overview

Add project save/load to the RF Simulator so that circuits, parameters, wiring, probe points, window layout, and subcircuit groups can be persisted to a `.rfsim` JSON file and restored on subsequent runs.

This is the #1 UX improvement: currently everything is ephemeral — building a circuit and closing the app means rebuilding it from scratch next time.

## Architecture

### Serialization API

Add two virtual methods to `IComponentEngine` (in `common/component_interface.h`):

```cpp
virtual nlohmann::json serialize() const { return nlohmann::json::object(); }
virtual void deserialize(const nlohmann::json&) {}
```

Each engine overrides with its own parameter data. Default no-op means engines that haven't implemented serialization yet will still save/load (with default params).

### Dependency: nlohmann/json

Add `nlohmann/json` via CMake FetchContent (header-only, well-tested, already proven in the old `feat/save-load-project` branch).

### Component Factory

A type-name → factory map on `ComponentRegistry`:

```cpp
template<typename T>
void registerType(const std::string& name) {
    m_factories[name] = [](NodeGraphEngine& g, ViewManager& v,
                            int id, const nlohmann::json& params) {
        auto comp = std::make_unique<T>(id, g);
        comp->deserialize(params);
        auto* ptr = comp.get();
        v.registerNode(&ptr->node());
        return ptr;
    };
}
```

Registered once at app startup for each component type. Load iterates the JSON array, looks up the factory by type string, calls it — no if/else chain.

## File Format (.rfsim)

```json
{
  "version": 1,
  "name": "MyCircuit",

  "components": [
    {
      "type": "SignalGenerator",
      "node_id": 1,
      "label": "Gen 1",
      "x": 100.0,
      "y": 200.0,
      "params": { /* type-specific */ }
    }
  ],

  "links": [
    { "link_id": 1, "start_pin": 101, "end_pin": 201 }
  ],

  "probe_pins": [ 101 ],

  "groups": [
    {
      "id": 50001,
      "name": "Mixer Chain",
      "member_node_ids": [3, 4, 5],
      "collapsed": true
    }
  ],

  "graph_state": {
    "next_node_id": 100,
    "next_pin_id": 200,
    "next_link_id": 1000
  },

  "window_state": {
    "log": true,
    "spectrum_analyzer": true,
    "properties": true,
    "node_editor": true,
    "iq_plots": [true, false]
  }
}
```

Boundary pins for collapsed groups are rebuilt on load (derived from cross-boundary links, not stored independently).

## Engine Serialize/Deserialize

| Component | Params Saved |
|---|---|
| **SignalGenerator** | `[{freq_Hz, power_dBm, phase_deg}, ...]` — tone list |
| **Amplifier** | `{gain_dB, nf_dB, enable_nonlinear, oip2_dBm, oip3_dBm}` |
| **Mixer** | `{lo_freq_Hz, conv_gain_dB}` |
| **Splitter** | (none — identity params) |
| **SParamEngine** | `{filepath, port_a, port_b}` |
| **AdcEngine** | `{sample_rate_Hz, bits, full_scale_V, nsd_dBm_per_Hz}` |
| **PFBChannelizer** | `{channel_count, taps_per_branch, kaiser_beta}` |
| **CoaxCable** | `{preset_index, length_m, connectors_loss_dB}` |
| **IdealFilter** | `{filter_type, cutoff_Hz, order}` |

Each is 5–15 lines of implementation.

## User Interaction

- **Ctrl+S** — Save (Save As if no path yet)
- **Ctrl+Shift+S** — Save As (always opens dialog)
- **Ctrl+O** — Open (unsaved changes → modal Save/Discard/Cancel, then dialog)
- **Ctrl+N** — New (unsaved changes → modal, then clear)
- **File menu bar** — New / Open / Save / Save As / Recent files list / Exit

**Unsaved-changes modal:** Simple blocking popup within the frame loop (not deferred-action pattern from old branch). Stays open until user picks Save/Discard/Cancel. This avoids the ImGui popup corruption the old branch suffered from.

**Auto-load:** Not on startup — opt-in via "Recent files" in File menu. Avoids the crash-loop problem from the old branch.

**Title bar:** Shows project filename with `*` when dirty.

**Dirty tracking:** `markDirty()` is called by every component-mutating operation (add/remove/param change/toggle). Cleared on save.

## Implementation Order

1. Add `nlohmann/json` to CMake FetchContent
2. Add virtual `serialize()`/`deserialize()` to `IComponentEngine`
3. Add factory registration API to `ComponentRegistry`
4. Implement serialize/deserialize on each engine
5. Add `saveProject()` / `loadProject()` / `newProject()` to app
6. Add file menu bar + keyboard shortcuts + unsaved-changes modal
7. Implement group serialization
8. Wire dirty tracking into all param-change callbacks
9. Add tests for round-trip save/load of each component type
10. DOX pass

## Components Not Yet Changed

No existing DSP or UI code needs modification. This is additive: adding serialization methods and a file menu. Existing behavior is unchanged when no file operation is used.

## Verification

- Round-trip test: save a circuit with every component type, load it, verify all params match
- Round-trip test: save with links + probes, load, verify graph topology restored
- Round-trip test: save with collapsed groups, load, verify groups intact
- Load invalid JSON → gracefully handled, app continues with default state
- Missing params on load → use defaults, no crash
- Dirty flag: set on any param change, cleared on save, prompt on unsaved close/new/open
