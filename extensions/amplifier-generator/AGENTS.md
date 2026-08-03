# extensions/amplifier-generator/AGENTS.md

## Purpose

Built-in `external-tool` extension that synthesizes Touchstone `.s2p` +
schema-v2 library JSON for amplifiers from datasheet parameters. Reference
implementation for the plugin extension system's `generator` capability.

## Ownership

- `plugin.json` — extension manifest (`kind: external-tool`,
  `capabilities: [generator]`, two `tools` menu actions)
- `bin/generate_amplifier.py` — entry point, dispatches on `action_label`
- `bin/params.py` — `load_params()` validates a params JSON, raises
  `ParamsError`
- `bin/touchstone.py` — unilateral S-parameter synthesis + `.s2p` writer
- `bin/library.py` — schema-v2 JSON writer, collision-safe output stems

## Local Contracts

- Fully headless: no interactive UI. The extension runner kills any tool
  process after 30s, so parameter entry happens via hand-edited JSON files,
  not an in-process dialog.
- Two-phase flow: "New Params Template..." scaffolds
  `rf-sim-generator-input/amplifier/params-N.json`; "Build from Params..."
  batch-processes every pending file there into
  `rf-sim-libraries/amplifiers/<manufacturer>/<part_number>.json` + `.s2p`,
  then moves the source into a `processed/` subfolder, suffixing
  `-2`, `-3`, … on the processed filename if that slot already exists.
- `parameters` field names in the generated JSON match
  `AmplifierEngine`'s factory in `app/src/component_type_registry.cpp`
  exactly: `nf_dB`, `oip3_dBm`, `p1db_dBm`, `oip2_dBm`, `gain_dB`.
- A `part_number`/`manufacturer` collision never overwrites — writes
  `<part_number>-2.json` (and matching `.s2p`) instead. See `README.md` for
  the manual regenerate/update workflow this implies.
- Python stdlib only (no PyQt5/numpy) — CI's MSYS2 Python has no pip
  packages installed. `extensions/*/bin/` is unignored in the repo's root
  `.gitignore` (the default `**/[Bb]in/*` rule would otherwise treat this
  source directory as build output).

## Work Guidance

- Keep `bin/*.py` modules independently unit-testable with no ImGui/app
  dependency; only the entry script wires them together.
- Any new menu action must update both `plugin.json`'s `menus[]` and the
  matching constant in `generate_amplifier.py` (`TEMPLATE_ACTION`/
  `BUILD_ACTION`) — they must match exactly since the runner passes the
  clicked label straight through as `action_label`.

## Verification

- `cd extensions/amplifier-generator && python -m unittest discover -s tests -v`
- C++ end-to-end: `build/bin/test_extensions "[amplifier-generator]"`

## Child DOX Index

*(none)*
