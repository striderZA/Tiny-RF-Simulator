# Runtime scenarios

Observable journeys through Tiny RF Simulator as it behaves **today**. Each journey
names the value a user gets, so a verifier can tell a pass from a fake. Runtime
execution and evidence belong to the shared Archon runtime-verification workflow;
this file only says what a real user does and what must be observably true.

## 1. A tone chain computes the cascade the user set up

1. Start the app. The default graph contains a signal generator node and an
   amplifier node, unconnected.
2. Wire the generator's output pin to the amplifier's input pin (drag the link, or
   the equivalent graph edit).
3. In the generator, set one tone at 100 MHz, 0 dBm.
4. Set the amplifier gain to +15 dB.
5. Open the spectrum analyzer and probe the amplifier output node.
6. The trace shows a single tone at 100 MHz at +15 dBm (within plot resolution),
   and the noise floor under the tone is raised consistently with the gain — not
   left at the generator's input floor.

**Value:** the number on the plot is the answer to the chain calculation the user
just built; if it disagreed with gain-in-dB the simulator would be worthless.

**What would make this fail:** the probed spectrum is stale after the gain change
(dirty-flag cache not invalidated), the tone power does not track the gain, or the
probe silently reads the generator node instead of the amplifier output.

## 2. A circuit survives save, restart and reopen

1. Build a chain: generator → attenuator (6 dB) → amplifier, wire it, probe the
   amplifier output.
2. Save to `chain.rfsim` via the File menu.
3. Make further edits, then reopen `chain.rfsim`.
4. Every node, its parameters (attenuator reads 6 dB), every wire and the probe
   come back. The unsaved-changes indicator is clean right after the reopen.
5. Close and relaunch the whole app; reopening the same file again restores the
   same graph.

**Value:** the user's work is on disk, not in RAM — they can stop mid-session and
trust the file.

**What would make this fail:** any node, wire, parameter, probe or selection that
round-trips differently, a dirty flag set by loading, or a failed save that clears
the dirty state.

## 3. A Touchstone part behaves like its datasheet file

1. In the component library browser (or a component's inspector), load a 2-port
   `.s2p` file for an amplifier definition.
2. Place the part and drive it with a single tone from the generator.
3. The output tone matches |S21| at the stimulus frequency (magnitude, dB) from the
   loaded file — and moving the tone frequency changes the output per the
   interpolated curve, not per a fixed gain.
4. The library browser shows the `[DATA]` indicator for the part.

**Value:** a user with a vendor S-parameter file can simulate that real part; the
plot must match what they measured.

**What would make this fail:** the fixed-gain path stays active after S-parameter
mode is selected, interpolation jumps between grid points instead of moving
smoothly, or the file parses without the engine ever consulting it.

## 4. The digitizer chain obeys its sample-rate contract

1. Wire: generator → amplifier → RF ADC → PFB channelizer. (A PFB fed directly from
   the RF chain, skipping the ADC, must be **rejected** — that rejection is part of
   this journey.)
2. Set the ADC input sample rate `Fs`, decimation `D = 4`, NCO at +0.25 × Fs. The
   PFB's input is the ADC's decimated stream at `Fs_adc = Fs / D`.
3. Configure the PFB with M channels at sampling ratio 1x (critical sampling): each
   channel outputs at `Fs_adc / M`, with M channel centers tiling `Fs_adc`.
4. Switch the PFB to sampling ratio 2x, leaving everything upstream alone: every
   channel's `fs_Hz` becomes `2 × Fs_adc / M` and its usable bandwidth doubles,
   while the channel **centers stay where they were** relative to the PFB input.
5. Separately, change the ADC decimation from 4 to 2: `Fs_adc` doubles, so the
   whole channel grid — spacing and centers — scales with it, and each channel's
   `fs_Hz` follows the doubled input. The NCO, being a normalized factor of the ADC
   input rate, keeps shifting the sampled spectrum to the same relative offset.

**Value:** these are the exact relationships a real channelized receiver obeys; the
user sizes their channel plan from these numbers.

**What would make this fail:** a direct RF→PFB link accepted; `fs_Hz` ignoring the
oversampling ratio; channel centers moving when only the ratio changes; channel
centers *not* moving when the ADC decimation changes; or decimation values other
than 1/2/4/8 being honored.
