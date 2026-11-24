# Component Library

The Component Library lists reusable parts you can place on the canvas. Open it from **View > Component Library**. Unlike other panels, its visibility is not remembered between runs — it starts hidden each launch.

## Browse parts

The panel groups parts in a collapsible tree: **type**, then **manufacturer**, then the parts. A row shows the part number and, when set, its description; a part with data files carries a `[DATA]` prefix. Hover a row for its manufacturer, type, notes, data files, and the JSON file it came from.

The filter box narrows the tree by part number, manufacturer, or description — not by type or notes.

Click a part to insert it into the circuit at the canvas origin. The new node takes the part's parameters and data files, shows the part number under its title, and is one undo step named `Add <part>`. A part with a valid Touchstone file starts in S-parameter mode; a part whose data file is missing still inserts and stays on its ideal model, with the load failure logged.

## Built-in parts

The repository ships 62 built-in definitions under `component_data/library`: 18 filters, 11 amplifiers, 6 fixed attenuators, 2 digital step attenuators, 5 splitters, 5 combiners, 5 mixers, 4 equalizers, and 6 ADCs. `AM1143` is the only built-in part with a Touchstone file; it inserts in S-parameter mode. Every other built-in part inserts on its ideal model. The release archives do not include them: a fresh download lists only the parts you create and parts from extensions. To use the built-in parts, copy the `component_data` folder from a source checkout into the folder beside the executable, so `component_data/library` sits there, and click **Refresh**.

| Type | Part | Manufacturer | Description |
|---|---|---|---|
| Filter | `MFLP-00033CSP1` | Mini-Circuits | Low-pass, 3.3 GHz cutoff |
| Filter | `SLP-50+` | Mini-Circuits | Low-pass, DC-48 MHz |
| Filter | `SLP-100+` | Mini-Circuits | Low-pass, DC-98 MHz |
| Filter | `SLP-450+` | Mini-Circuits | Low-pass, DC-400 MHz |
| Filter | `SLP-1000+` | Mini-Circuits | Low-pass, DC-900 MHz |
| Filter | `SLP-2400+` | Mini-Circuits | Low-pass, DC-2200 MHz |
| Filter | `BHP-500+` | Mini-Circuits | High-pass, 500 MHz cutoff |
| Filter | `SHP-50+` | Mini-Circuits | High-pass, 41-800 MHz passband |
| Filter | `SHP-100+` | Mini-Circuits | High-pass, 90-2000 MHz passband |
| Filter | `SHP-250+` | Mini-Circuits | High-pass, 225-3000 MHz passband |
| Filter | `SHP-1000+` | Mini-Circuits | High-pass, 1000-3000 MHz passband |
| Filter | `BFC-160+` | Mini-Circuits | Band-pass, 140-180 MHz |
| Filter | `SBP-70+` | Mini-Circuits | Band-pass, 63-77 MHz |
| Filter | `VBFZ-925-S+` | Mini-Circuits | Band-pass, 800-1050 MHz |
| Filter | `VBFZ-2000+` | Mini-Circuits | Band-pass, 1730-2270 MHz |
| Filter | `BSF-C75+` | Mini-Circuits | Band-stop, 65-85 MHz |
| Filter | `BSF-C100+` | Mini-Circuits | Band-stop, 90.365-109.635 MHz |
| Filter | `BSF-C160+` | Mini-Circuits | Band-stop, 150.3-169.7 MHz |
| Amplifier | `AM1143` | Anatech Electronics | Low-noise amplifier, with Touchstone data |
| Amplifier | `MGA-62563` | Broadcom | Low-noise LNA, 900 MHz |
| Amplifier | `ZX60-33LN+` | Mini-Circuits | Low-noise gain block, 1 GHz |
| Amplifier | `PGA-103+` | Mini-Circuits | Ultra-low-noise gain block, 0.05-4 GHz |
| Amplifier | `ZX60-83LN+` | Mini-Circuits | Wideband LNA, 0.5-8 GHz |
| Amplifier | `GALI-39+` | Mini-Circuits | MMIC gain block, DC-7 GHz |
| Amplifier | `ERA-5SM+` | Mini-Circuits | MMIC gain block, DC-4 GHz |
| Amplifier | `MAR-6SM+` | Mini-Circuits | Low-current MMIC gain block, DC-2 GHz |
| Amplifier | `SBB5089Z` | Qorvo | InGaP HBT gain block, 50 MHz-6 GHz |
| Amplifier | `ZHL-2-8+` | Mini-Circuits | Driver amplifier, 10-1000 MHz |
| Amplifier | `ZHL-42W+` | Mini-Circuits | Driver amplifier, 10-4200 MHz |
| Attenuator | `ATN01-0040PSM` | Anatech Electronics | Fixed coaxial, 1 dB |
| Attenuator | `RC-10-10+` | Mini-Circuits | Fixed coaxial, 10 dB |
| Attenuator | `RCAT-03+` | Mini-Circuits | Fixed SMT, 3 dB, DC-20 GHz |
| Attenuator | `RCAT-06+` | Mini-Circuits | Fixed SMT, 6 dB, DC-20 GHz |
| Attenuator | `RCAT-20+` | Mini-Circuits | Fixed SMT, 20 dB, DC-20 GHz |
| Attenuator | `RCAT-30+` | Mini-Circuits | Fixed SMT, 30 dB, DC-20 GHz |
| Digital step attenuator | `ZX76-31R5A-PN+` | Mini-Circuits | 6-bit, 0.5 dB steps, DC-4 GHz |
| Digital step attenuator | `DAT-31R5A-PP+` | Mini-Circuits | 6-bit, 0.5 dB steps, DC-4 GHz |
| Splitter | `M1-0002` | Marki Microwave | 2-way, 2-18 GHz |
| Splitter | `MPD-0226CH` | Mini-Circuits | 2-way, 10-2600 MHz |
| Splitter | `ZFSC-2-2-S+` | Mini-Circuits | 2-way, 10-1000 MHz |
| Splitter | `ZFSC-2-4-S+` | Mini-Circuits | 2-way, 0.2-1000 MHz |
| Splitter | `ZN2PD2-63-S+` | Mini-Circuits | 2-way, 350-6000 MHz, 25 W |
| Combiner | `M1-0218` | Marki Microwave | 2-way, 2-18 GHz |
| Combiner | `ZFSC-2-1G-S+` | Mini-Circuits | 2-way, 10-2500 MHz |
| Combiner | `PD-0218` | Marki Microwave | 2-way Wilkinson, 2-18 GHz |
| Combiner | `ZFSC-2-2-S+` | Mini-Circuits | 2-way, 10-1000 MHz |
| Combiner | `ZN2PD2-63-S+` | Mini-Circuits | 2-way, 350-6000 MHz |
| Mixer | `ADL5387` | Analog Devices | I/Q demodulator, 400-2700 MHz |
| Mixer | `ZAD-1-1+` | Mini-Circuits | Double-balanced, 0.001-1000 MHz |
| Mixer | `ZEM-4300+` | Mini-Circuits | Double-balanced, 300-4300 MHz |
| Mixer | `ZX05-73L+` | Mini-Circuits | Wideband, 2400-7000 MHz |
| Mixer | `ADL5801` | Analog Devices | Active, 100-6000 MHz |
| Equalizer | `EQ-12` | Anatech Electronics | -12 dB/decade slope |
| Equalizer | `MEQ0-20ASM` | Marki Microwave | 0-20 GHz, negative slope |
| Equalizer | `EQY-4-123+` | Mini-Circuits | 4 dB fixed slope, DC-12 GHz |
| Equalizer | `EQY-9-123+` | Mini-Circuits | 9 dB fixed slope, DC-12 GHz |
| ADC | `AD9226` | Analog Devices | 12-bit, 65 MSPS |
| ADC | `ADC12D1600` | Texas Instruments | 12-bit, 3.2 GSPS |
| ADC | `AD9680` | Analog Devices | 14-bit, 1.25 GSPS dual |
| ADC | `AD9208` | Analog Devices | 14-bit, 3 GSPS dual |
| ADC | `LTC2208` | Analog Devices | 16-bit, 130 MSPS |
| ADC | `ADS54J60` | Texas Instruments | 16-bit, 1 GSPS dual |

Built-in definitions have no **Edit** button; they cannot be changed in place.

## Author a part

**New Component...** opens the **Component Form**:

1. Pick a **Type**. Changing type rebuilds the form, discarding typed values.
2. Fill **Part Number** (required), and optionally **Manufacturer**, **Description**, and **Notes** — the description shows in the browser, the notes in the tooltip.
3. Set the type's parameters, for example **Gain (dB)** for an amplifier, or the filter type and cutoffs for a filter. Each field shows its range; a field outside it is flagged in red, and **Save** stays disabled until every issue is fixed.
4. Types that support it offer an **S-parameter File** picker (`.s2p` through `.s9p`). The file is copied beside the definition, so the part travels with its data.
5. Pick **Save To**: **Project** (`./rf-sim-libraries`, relative to the folder the simulator was started from) or **Global** (`~/.rf-sim/libraries` on Linux, `%USERPROFILE%\.rf-sim\libraries` on Windows).

**Save** writes the definition as JSON — `<root>/<type>/<manufacturer>/<part number>.json` — and the part appears in the browser immediately, no **Refresh** needed. Saving marks the project dirty even though the circuit did not change, and [undo](undo-redo.md) does not remove the file.

A part's **Edit** button (user-defined parts only) reopens the same form pre-filled and writes back to the same file. Editing a definition affects future inserts; components already on the canvas keep their copied parameters until you swap them.

## Refresh

Click **Refresh** to rescan every library root — global, project, built-in, and extension data packs. Use it after editing or deleting definition files outside the app, or after copying `component_data` beside the executable. Refresh never touches placed components or the project's dirty state.

## Swap a placed part

Select a node that was inserted from the library. The Properties panel shows a **Part** list under the component's name — every part in the library, not just the current type. Pick another part to replace it in place.

The successor keeps:

- the node's id, canvas position, and selection;
- its links, reattached by port index — input 1 to input 1, output 1 to output 1;
- its probes, group membership, and the [Network Analyzer](network-analyzer.md) and [Power Meter](power-meter.md) points that name it;
- one undo step, named `Change <old part> to <new part>`.

It starts from the new part's library parameters, not the old part's edited values. A swap is refused, with the reason shown under the list, when a connected, probed, or instrument-pointed port has no match on the new part — so a linked splitter output blocks a change to a one-output part — or when the new part cannot be built.

## Extensions and agents

Extension data packs can contribute library roots of their own; manage extensions under **Tools > Extensions**. An [agent](agent-mcp.md) can create validated JSON-only parts with `library_create` — they appear in the browser immediately.
