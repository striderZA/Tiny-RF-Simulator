# Amplifier Data-File Generator

Built-in `external-tool` extension. Synthesizes a Touchstone `.s2p` + schema-v2
library JSON pair for an amplifier from datasheet parameters, using a
unilateral S-parameter approximation (S21 from gain-vs-freq, S11/S22 from a
fixed assumed return loss, S12 = 0).

## Workflow

1. **Tools > Amplifier: New Params Template...** — writes a numbered template
   (`rf-sim-generator-input/amplifier/params-N.json`) into the current
   project. Runs instantly; no dialog opens (external tools are killed after
   30s, so parameter entry can't be interactive — edit the file directly).
2. Edit that JSON file in any text editor. Required fields: `part_number`,
   `manufacturer`, `gain_db_vs_freq` (>=2 `[freq, gain_dB]` points, strictly
   increasing frequency). Optional: `nf_dB`, `oip3_dBm`, `p1db_dBm`,
   `oip2_dBm`, `gain_dB`, `input_return_loss_db` / `output_return_loss_db`
   (default 20 dB each), `description`, `notes`.
3. **Tools > Amplifier: Build from Params...** — processes every pending
   `*.json` file directly under `rf-sim-generator-input/amplifier/` (not its
   `processed/` subfolder). Each one becomes
   `rf-sim-libraries/amplifiers/<manufacturer>/<part_number>.json` + matching
   `.s2p`, then the source params file moves into `processed/`. Malformed
   files are skipped (reported in the result message) and left in place for
   you to fix and rerun. Newly built parts appear after the app's automatic
   post-run library rescan.

## Regenerating / updating a part

A `part_number`/`manufacturer` collision is never silently overwritten — a
second build for the same identity writes `<part_number>-2.json` instead. To
replace a part after fixing a mistake:

1. Delete the stale `<part_number>.json` and `<part_number>.s2p` from
   `rf-sim-libraries/amplifiers/<manufacturer>/`.
2. Move the original params file back out of
   `rf-sim-generator-input/amplifier/processed/` into
   `rf-sim-generator-input/amplifier/` (or edit a fresh template).
3. Run **Build from Params...** again.

## Limitations (v1)

- Amplifiers only — mixer/passive/multi-stage generators are separate future
  extensions following the same pattern.
- Unilateral approximation only: no S12, no frequency-dependent phase, fixed
  assumed return loss unless overridden per-part.
- No interactive parameter entry (extension-system process timeout is 30s).
