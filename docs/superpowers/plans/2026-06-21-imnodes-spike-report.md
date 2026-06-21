# imnodes API Spike Report

**Date:** 2026-06-21
**Spike goal:** Verify imnodes API surface for subcircuit groups.

**imnodes version pinned:** `https://github.com/Nelarius/imnodes.git` (FetchContent in `CMakeLists.txt` lines 62-69; revision at `build/_deps/imnodes-src`).

## API: `ImNodes::GetNodeGridSpacePos(int node_id)`

- **Status:** **Present** but with a different signature than the plan assumed.
- **Verified at:** `build/_deps/imnodes-src/imnodes.h:348`, `build/_deps/imnodes-src/imnodes.cpp:2791-2799`.
- **Actual signature:** `ImVec2 GetNodeGridSpacePos(const int node_id)` — returns the position by value.
- **Plan was written against:** `ImNodes::GetNodeGridPos(int node_id, ImVec2* out)` — a different signature that does not exist in this imnodes revision.
- **Action:** The plan and any implementer code must use `GetNodeGridSpacePos(int)` returning `ImVec2`. All references to `GetNodeGridPos` in `docs/superpowers/plans/2026-06-21-subcircuit-groups.md` and downstream code must be updated.

**Related API:**
- `ImNodes::GetNodeScreenSpacePos(int)` — returns screen-space position
- `ImNodes::GetNodeEditorSpacePos(int)` — returns editor-space position
- `ImNodes::GetNodeDimensions(int)` — returns `ImRect.GetSize()` as `ImVec2`
- `ImNodes::SetNodeGridSpacePos(int, const ImVec2&)` — set grid position

## API: Screen↔grid transform

- **Status:** **Not present** as a function in this revision. imnodes has panning but **no zoom**.
- **Verified at:** No matches for `ScreenToGrid` or `GridToScreen` in the imnodes source.
- **Equivalents:**
  - `ImNodes::EditorContextGetPanning()` returns the current panning `ImVec2` (from `imnodes.h:254`).
  - `ImNodes::EditorContextResetPanning(const ImVec2&)` resets panning.
  - In grid space, the origin is the upper left corner of the editor window translated by the panning vector. There is no zoom factor.
- **Implication for the rubber-band:**
  - The rubber-band is drawn in screen space via `GetWindowDrawList()->AddRect()` and does not need transformation.
  - For node hit-testing, the rubber-band's screen-space rectangle can be compared against each node's `GetNodeScreenSpacePos(int)` (which already accounts for panning). This avoids any manual coordinate transform.
- **No fallback needed.**

## API: Phantom node rendering

- **Status:** **Not needed.**
- **Verified by:** reading `imnodes.cpp:561-568` (`GetScreenSpacePinCoordinates`) and `imnodes.cpp:2505-2514` (`EndNode` updates `node.Rect`).
- **Analysis:**
  - Pin position is computed from `editor.Nodes.Pool[pin.ParentNodeIdx].Rect` at link-render time (`imnodes.cpp:558-568`).
  - `node.Rect` is updated by `EndNode()` (`imnodes.cpp:2511`).
  - If we skip `BeginNode`/`EndNode` for an internal node while the group is collapsed, the `Rect` from the previous frame remains in the pool.
  - As long as the internal node's position does not change while collapsed (it cannot, because the user cannot interact with it), the cached `Rect` is accurate.
  - When the user expands, drags an internal node, then collapses again, the position is updated during the expand phase and remains accurate while collapsed.
  - The group block's position is read-only in v1 (centroid of internals), so the group block never moves either.
- **Conclusion:** Cross-boundary links render correctly without phantom nodes, because imnodes' position pool is stable. The `m_use_phantom_nodes` flag should be left at `false` in the widget. Task 14 (phantom node workaround) becomes a no-op for v1: the flag stays false and `drawPhantomNodes` can be an empty function.

## Conclusion

Three design changes from the original plan:

1. **Use `GetNodeGridSpacePos(int)` returning `ImVec2`**, not `GetNodeGridPos(int, ImVec2*)`. All code and the plan must be updated to match.
2. **No `ScreenToGrid` / `GridToScreen` transform needed** for the rubber-band. Use `GetNodeScreenSpacePos` for hit-testing, draw the rubber-band rectangle in screen space.
3. **Phantom node workaround is not needed.** `m_use_phantom_nodes` stays `false`. Task 14 of the plan can be reduced to "verify phantom workaround is not needed; do nothing."

No other design changes are required. The data model, ID spaces, engine API, and widget rendering strategy from the spec all hold.
