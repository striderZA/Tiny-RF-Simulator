# Promo GIFs

Finished GIFs for the Tiny RF Simulator promo series live here: eight square feature episodes
and one landscape hero, covering every user-visible feature since v0.28.2. Each GIF is produced
from a capture of the real app — never from a rebuilt UI.

The source workspace (HyperFrames projects, raw captures, masters) lives outside this repository
at `E:/Jaco/Projects/rf-sim/gif-series` (`$GIF_SERIES` below). Only the finished GIFs and this
README are committed here.

## Deliverables

Episodes are 1080×1080 and the hero is 960×540. All are 20 fps, loop forever, and must stay under
5 MB (the hard cap for X mobile and GitHub). The screen band is always a **1:1 crop** of the
1920×1080 client area captured with the UI font at 20 px; the app is never downscaled to fit.
Crops are padded with the frame background when the panel is narrower than the canvas.

| GIF | What it shows | App crop (native pixels) | Output | Target | Budget |
|---|---|---|---|---|---|
| `trs-00-whats-new.gif` | Hero montage: align/snap, markers, IM3, single-sided, budget, font size | two 960×540 1:1 crops (node editor, spectrum) | 960×540 | 7 s | ≤5 MB |
| `trs-01-editing.gif` | Undo/redo, align, snap, removal notice | Node Editor region, 1:1 | 1080×1080 | 8 s | ≤5 MB |
| `trs-02-markers.gif` | M1–M4, delta, Marker Table, Peak Table, Measure on | Spectrum Analyzer panel, 1:1 | 1080×1080 | 9 s | ≤5 MB |
| `trs-03-harmonics-im3.gif` | Two-tone chain, Harmonics, IM3, TOI | Spectrum Analyzer panel, 1:1 | 1080×1080 | 10 s | ≤5 MB |
| `trs-04-single-sided.gif` | Single-sided fold, complex ADC probe, two-column config, Ref +60 dBm | Spectrum Analyzer panel, 1:1 | 1080×1080 | 8 s | ≤5 MB |
| `trs-05-rf-budget.gif` | Named mode, horizontal chain, cascade, OOB/SNR verdicts | RF Budget + Receiver Requirements, 1:1 | 1080×1080 | 12 s | ≤5 MB |
| `trs-06-parts.gif` | 62-part library, DSA, S-param splitter, in-place swap, refresh | Component Library + Node Editor, 1:1 | 1080×1080 | 10 s | ≤5 MB |
| `trs-07-agent.gif` | Real MCP terminal beside the app: get/edit/library/budget/save | split: terminal + app, 1:1 crops | 1080×1080 | 8 s | ≤5 MB |
| `trs-08-settings.gif` | Settings menu, font family/style/size, layouts, updates, Utility Space | Settings + Utility Space, 1:1 | 1080×1080 | 8 s | ≤5 MB |

## Regeneration

Set the workspace path once (it is outside this repository and is not committed):

```bash
GIF_SERIES=${GIF_SERIES:-E:/Jaco/Projects/rf-sim/gif-series}
```

1. Capture the raw footage. The harness cleans `app.ini`/`rf_simulator_layout.ini`, launches the
   app from the repository root, sets the UI font to 20 px, replays the episode's MCP setup, and
   records the 1920×1080 client area at 60 fps (H.264 CRF 15, yuv420p, SDR, cursor visible):

   ```bash
   pwsh "$GIF_SERIES/scripts/capture.ps1" \
     -Output captures/02-markers.mp4 -Duration 30 \
     -Setup "$GIF_SERIES/scripts/setup/02-markers.json"   # per-episode MCP setup
   ```

   `scripts/setup/demo-circuit.json` is the harness-test setup; each episode adds its own
   `scripts/setup/<number>-<slug>.json` with the circuit, probes and panels that episode needs.

2. Author or update the episode in `$GIF_SERIES/episodes/<number>-<slug>/` and check it:

   ```bash
   cd "$GIF_SERIES/episodes/02-markers"
   npm run check
   ```

3. Render the MP4 master (full chroma for UI text) and encode the GIF:

   ```bash
   npx --yes hyperframes@0.9.63 render --quality delivery --fps 30 \
     --chroma-subsampling 444 --output ../../renders/trs-02-master.mp4
   bash "$GIF_SERIES/scripts/encode-gif.sh" \
     ../../renders/trs-02-master.mp4 ../../renders/trs-02-markers.gif 1080:1080
   ```

   The encoder is fixed: Lanczos scale, `palettegen=stats_mode=diff:max_colors=256`,
   `paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle`, `-loop 0`, 20 fps. The hero uses
   `960:540`.

4. Gate the result (resolution, 20 fps, duration, loop forever, ≤5 MB), then copy it here:

   ```bash
   pwsh "$GIF_SERIES/scripts/check-gifs.ps1" -Path "$GIF_SERIES/renders/trs-02-markers.gif"
   cp "$GIF_SERIES/renders/trs-02-markers.gif" assets/gifs/
   ```

## Rules

- No rebuilt app UI, no mocked readouts, no claims beyond the visible interface.
- The UI font is 20 px at capture; the smallest label must stay legible at 100% and 75% zoom.
- Do not commit raw captures, MP4 masters or the HyperFrames projects; they stay in the external
  workspace.
