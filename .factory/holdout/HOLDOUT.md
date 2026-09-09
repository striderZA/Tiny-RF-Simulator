# Holdout scenarios

Independent composed scenarios that combine this product's behaviors differently
from `harness/END-TO-END.md`. They describe behavior as it exists today. The
producer must establish isolation and start a fresh environment for this pass; the
Markdown below is the scenario spec, and any private evaluator JSON derived from it
must stay outside the builder's accessible checkout (see FACTORY.md, "Holdout
isolation").

## 1. A rejected topology never persists

1. Start with fresh state and wire generator → amplifier → RF ADC → PFB.
2. Attempt to link the amplifier output **directly** into a second PFB's input,
   bypassing any ADC. The link must be refused.
3. Save the project to `holdout.rfsim`; restart the app; reopen it.
4. The refused link is absent from the file and from the graph; the valid
   ADC→PFB chain and every channel's center/`fs_Hz` are exactly as they were
   before the save.

**Catches:** a rejection that only exists in the UI gesture layer while the engine
accepts the edge; or a save path that persists attempted-but-refused wiring.

## 2. Oversample toggle survives a save/reload in the middle

1. Fresh state: generator → amplifier → ADC at `Fs`, decimation 4 → PFB with M
   channels at ratio 1x. Note channel 0's center frequency and `fs_Hz`.
2. Save. Reopen. Switch the PFB to ratio 2x. Note center and `fs_Hz` again:
   center unchanged, `fs_Hz` doubled, bandwidth doubled.
3. Save over the same file. Reopen. The persisted ratio is 2x, the channel
   centers are the same ones observed in step 2, and the legacy default (1x)
   is NOT restored.
4. Set decimation to 2 in the ADC. Channel centers move with the PFB input rate
   (grid doubles), per the engine contract.

**Catches:** serialization that drops `sampling_ratio` (silently reverting a user
to critical sampling), or a loader that recomputes channels from defaults instead
of persisted state.

## 3. Two S-parameter parts in cascade with a legacy file underneath

1. Create an attenuator and an amplifier both driven by 2-port Touchstone files
   (a nominal 6 dB pad, a flat-gain amp); wire generator → pad → amp.
2. Probe the amp output with a single tone: total chain gain equals the sum of the
   interpolated |S21| magnitudes at the tone frequency, within plot resolution.
3. Move the tone to a frequency between S-param grid points: the reading moves
   smoothly per the interpolation of both files — it must not snap to a grid point
   or a fixed-gain constant.
4. Separately, load a legacy `.rfsim` written before S-parameter modes existed
   (defaults apply: ADC decimation 2, NCO +0.25×Fs, PFB 1x): it opens clean, and
   saving it again must still round-trip through the current loader.

**Catches:** per-component S-param handling that never composes across a cascade;
legacy-default injection that fires on save as well as load; interpolation applied
in magnitude but not in dB.

## 4. Delete-under-dirty-cascade leaves no stale reading

1. Fresh state: generator → amp(15 dB) → splitter → two attenuators, each
   branch probed. Both probes show their expected powers.
2. Delete the splitter's link to one branch. The surviving branch's probe must
   keep its exact previous reading; the orphaned probe's node must report the
   de-wired state (no signal), not a stale cascade value.
3. Undo nothing; save, restart, reopen. Graph, the remaining branch's reading and
   the orphaned branch's empty state all round-trip — the dirty-flag cache may
   not re-energize the deleted path after reload.
4. Re-wire the deleted link. The orphaned probe must recompute to its original
   value without an app restart.

**Catches:** cache invalidation that only propagates downstream (the untouched
branch is also invalidated — benign), or the inverse: invalidation that misses the
reconnected branch so a stale "no signal" sticks after re-wiring.
