# Component Library

The Component Library lists reusable parts you can place on the canvas. Open it from **View > Component Library**. Unlike other panels, its visibility is not remembered between runs — it starts hidden each launch.

## Browse parts

The panel groups parts in a collapsible tree: **type**, then **manufacturer**, then the parts. A row shows the part number and, when set, its description; a part with data files carries a `[DATA]` prefix. Hover a row for its manufacturer, type, notes, data files, and the JSON file it came from.

The filter box narrows the tree by part number, manufacturer, or description — not by type or notes.

Click a part to insert it into the circuit at the canvas origin. The new node takes the part's parameters and data files, shows the part number under its title, and is one undo step named `Add <part>`. A part with a valid Touchstone file starts in S-parameter mode; a part whose data file is missing still inserts and stays on its ideal model, with the load failure logged.

## Built-in parts

The repository ships 18 built-in definitions under `component_data/library` — amplifiers, attenuators, filters, splitters, mixers, equalizers, combiners, and ADCs, including one amplifier (`AM1143`) with a Touchstone file. The release archives do not include them: a fresh download lists only the parts you create and parts from extensions. To use the built-in parts, copy the `component_data` folder from a source checkout into the folder beside the executable, so `component_data/library` sits there, and click **Refresh**.

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
