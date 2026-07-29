# tutorial/AGENTS.md

## Purpose
Guided first-run walkthrough: a data-driven sequence of steps that highlights the
panel each step talks about and remembers, across restarts, that the user has
already been offered the tutorial.

## Ownership
- `tutorialSteps()` / `tutorialTargetWindowTitle()` — the step catalog and the
  `TutorialTarget` → ImGui window-title mapping
- `TutorialState` — navigation state machine plus the durable completion marker
- `TutorialWidget` — the target highlight and the "Tutorial Guide" window

## Local Contracts
- Step content is data-driven: add, reorder, or remove entries in the `steps`
  array in `tutorial_steps.cpp`. Nothing else hard-codes a step count or index.
- `tutorialTargetWindowTitle()` must return the exact string `app/` passes to
  that panel's `draw()` call. A mismatch silently drops the highlight rather
  than failing, so change both sides together.
- Completion is an exe-relative marker file, `<exe_dir>/.tutorial_completed`,
  detected the same way as `LayoutManager` (see `layout/AGENTS.md`).
  Deliberately not `SessionState`, which is a no-op outside Windows and would
  make the first-run prompt reappear on every launch on Linux/macOS.
- Only finishing the last step marks completed from inside the tutorial; `Exit`
  and `Skip` do not. Dismissing the first-run offer in `app/` also marks it —
  that prompt is a one-time offer, not a recurring reminder.
- `TutorialState` is pure logic and `std::filesystem` only — no `ImGuiContext`,
  so it stays unit-testable under `tests/`. Everything needing a live context
  lives in `TutorialWidget`.
- The highlight is drawn on `ImGui::GetForegroundDrawList()`, so it paints over
  every regular window. The guide window is pinned to the bottom-right of the
  main viewport to stay clear of the panels it highlights.

## Work Guidance
- Steps target whole panels only. `ImGui::FindWindowByName` resolves windows,
  and `NodeGraphWidget` exposes no per-node or per-pin screen rect, so
  pin-level highlighting needs new plumbing in `node_graph/` first.
- Keep instruction wording verified against actual app behavior, not against
  `help/`'s reference text — the two can drift.

## Verification
- `ctest --test-dir build -R test_tutorial_state --output-on-failure` (pure
  navigation + marker-file tests, no ImGui context)
- `build/bin/test_ui` — `tutorial_launches_from_help_menu`,
  `tutorial_start_guards_unsaved_changes`, `tutorial_step_navigation`,
  `tutorial_completes_and_persists`,
  `tutorial_first_run_prompt_marks_completed`

## Child DOX Index
*(none)*
