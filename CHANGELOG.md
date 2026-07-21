## [0.9.1] - 2026-07-21

### Features

- **Part number display** — component blocks in the node editor show the library part number as a subtitle below the title bar
- **7 new component categories** — attenuators, splitters, filters, mixers, equalizers, combiners, and ADCs added to the component library with JSON definitions and instantiation code

### Testing

- Part number propagation test verifies instantiation sets the graph node field
- 131 total tests pass, zero regressions

## [0.9.0] - 2026-07-21

### Features

- **Component library manager** — file-based library browser with global and per-project libraries
  - JSON-based component definitions with datasheet parameters (gain, NF, OIP3, P1dB)
  - Library browser panel with tree view (grouped by type → manufacturer) and text filter
  - Three scan roots: built-in examples, global (~/.rf-sim/libraries/), per-project (./rf-sim-libraries/)
  - One-click insert into node graph via View menu
- **P1dB parameter** — first-class 1-dB compression point support
  - Added to NonlinearModel with automatic OIP3 ↔ P1dB derivation (OIP3 = P1dB + 9.6 dB)
  - AmplifierEngine exposes P1dB with serialize/deserialize for project save/load
  - Inspector panel: editable P1dB field (no longer an estimate)
- **Example components** — 3 real-world amplifier definitions
  - AM1143 (Anatech Electronics) — 30 dB LNA, NF 1.5 dB
  - ZX60-33LN+ (Mini-Circuits) — 28 dB gain block, NF 1.1 dB
  - MGA-62563 (Broadcom) — 15 dB LNA, NF 1.3 dB

### Bug Fixes

- _(ci)_ Upgrade GitHub Actions to resolve Node.js 20 deprecation

### Testing

- 12 new tests (190 total, 65 398 assertions, zero regressions)
- P1dB round-trip test verifies save/load persistence

## [0.8.4] - 2026-07-20

### Features

- **Duplicate components** — right-click any component in the node graph to duplicate it with all parameters. The duplicate appears offset from the original. Connections are not copied. Supports all 12 component types including PFB Channelizer (creates associated IQ plot and grid widgets).

## [0.8.2] - 2026-07-17

### Bug Fixes

- Fix marker applying to hidden signals in spectrum analyzer. Marker now only considers actively displayed traces when snapping to peaks. Added per-trace visibility tracking that syncs with ImPlot legend state.

## [0.8.1] - 2026-07-16

### Bug Fixes

- Fix crash when adding components via right-click context menu in the node graph. `detectNodeMoves()` called `GetNodeEditorSpacePos()` on newly-added nodes not yet registered with imnodes, hitting an assertion. Clear `m_node_screen_positions` each frame so only nodes drawn by imnodes are queried.

## [0.8.0] - 2026-07-16

### Features

- **Project save/load** — persist entire circuits to `.rfsim` JSON files
  - File menu with New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S)
  - Unsaved-changes dialog with Save/Discard/Cancel on close or new/open
  - Dirty tracking: parameter edits, node moves, link changes, component add/remove
  - Full round-trip fidelity for all 12 component types
  - Graph state restoration: node positions, wiring, probe visibility
  - 9 project file round-trip tests (55 assertions)
- Serialize/deserialize on all engine types: SignalGenerator, Amplifier, Splitter, Mixer, ADC, PFB Channelizer, Coax Cable, Ideal Filter, Attenuator, Combiner, Equalizer
- Amplifier and IdealFilter S-parameter mode state preserved across save/load
- Graph state helpers: `setNextIds`, `removeAllLinks`, group/boundary-pin ID restore

### Bug Fixes

- Fix link restoration when unknown component types are skipped during load
- Fix deserialize defaults to match constructor defaults (ADC, Mixer, PFB)
- Fix PFB active_channel clamping safety (bounds check, M=0 guard)
- Add FilterType enum validation on IdealFilter deserialize
- Clear position cache on node deletion to prevent stale entries
- Fire onLinkChanged when node deletion implicitly removes links

### Tests

- 178 test cases, 65,368 assertions (up from 166)
- Parameter value round-trip verification (gen/amp/mixer/coax)
- Group save/load round-trip test
- Attenuator/Combiner/Equalizer round-trip test
- Invalid JSON load test, unique temp paths for parallel safety

## [0.7.1] - 2026-07-14

### Bug Fixes

- Fix CI test failures: use PROJECT_SOURCE_DIR for S-param file paths in standalone test executables
- Set WORKING_DIRECTORY for test_attenuator and test_combiner ctest registrations

## [0.7.0] - 2026-07-14

### Features

- Add passive attenuator component with manual dB control and S-parameter mode
  - Physically accurate noise model (NF = attenuation)
  - Touchstone S-param support (2-port .s2p files)
  - Zigzag schematic symbol in node graph
  - Inspector panel with attenuation slider and S-param file picker
  - 10 unit tests
- Add combiner component (2-input → 1-output passive RF combiner)
  - Wilkinson model with -3 dB loss per input (exact dual of splitter)
  - Coherent signal combination with phase preservation
  - S-parameter mode with 3-port Touchstone support (.s3p files)
  - Y-shaped schematic symbol in node graph
  - Inspector panel with S-param mode toggle
  - 6 unit tests

### Documentation

- Prepare repository for open source release
- Add MIT license, README, CONTRIBUTING.md, AGENTS.md
- Update ROADMAP with attenuator and combiner as completed

## [0.6.0] - 2026-07-13

### Bug Fixes

- Stale binary name in runbook, uncalibrated coax preset comments
- Replace MSVC with MinGW-w64 in bug report template
- Gitignore `.agents/` and scope Testing pattern to root only

### Documentation

- Clean up AGENTS.md child DOX index for public release
- Mark PFB channelizer as completed in roadmap
- Update test counts (73 → 166) and track testing guidance

### CI

- Fix openwiki workflow description (manual, not scheduled)

## [0.5.1] - 2026-07-12

### Bug Fixes

- Correct window title

### Documentation

- Update docs to reflect exe rename

### Miscellaneous Tasks

- Remove CLAUDE.md from openwiki workflow
- Add release template
- Update build artifacts
- Bump version to 0.5.1

### Refactoring

- Change exe name
## [0.5.0] - 2026-07-12

### Bug Fixes

- Change touchstone test path to match current repo layout

### Documentation

- Add openwiki framework for documentation

### Features

- Add openwiki to git workflows

### Miscellaneous Tasks

- Delete stale design docs
- Bump version to 0.5.0

### Adc

- Remove dead bits/v_fs params, clamp Fs, use dbToLinear

### Coax

- Fix phase calculation (remove redundant 1e-3), clamp connector loss, correct MT 340 preset

### Equalizer

- Add NaN guards for log10(0), clamp ref freq

### Iq-plot

- Extract DSP to testable function, add Fs guard, add tests

### Touchstone

- Add input validation, clamp log10(0), use lower_bound
## [0.4.0] - 2026-07-06

### Bug Fixes

- _(node-graph)_Balance setupDarkTheme PopStyleColor at frame end
- Address final code review findings (NF scaling, fs_Hz, includes, tests)
- Enable S-param mode switching on amplifier mode combo The amplifier mode combo only called setSParamMode(false) when switching to Ideal, but did nothing when switching to S-Parameter. Filter and equalizer combos handled both directions correctly.

### Documentation

- Project save/load design spec
- Project save/load implementation plan
- Ponytail cleanup design spec
- Ponytail cleanup implementation plans (1-4)
- Node graph appearance design spec
- Node graph appearance implementation plan
- Add S-param rework spec and implementation plan

### Features

- _(node-graph)_Add NodeKind enum and label lookup
- _(node-graph)_Add themeColor helper with palette
- _(node-graph)_Apply dark theme to node editor canvas
- _(node-graph)_Color-code nodes by component type
- _(node-graph)_Add 10 schematic symbol helpers (vector)
- _(node-graph)_Draw schematic symbol on each node body
- _(node-graph)_Themed collapsed group block with symbol
- _(amplifier)_Add S-parameter mode to AmplifierEngine Add S-parameter mode as an alternative gain path alongside the existing ideal gain mode. Key changes: - Add SParameterData member, filepath, mode flag, and S21 index - Add setSParamFilepath(), sparamMode(), setSParamMode(), sparamLoaded(), sparamFilepath(), sparamData() accessors - Insert S-parameter processing branch in update() that applies complex S21 gain, S21-based nonlinearity, and |S21|² noise scaling - Update hoverSummary() to show S-Param Amp when in S-param mode - Add link to simulator::touchstone_parser in CMakeLists - Update inspector panel UI with Mode combo, file browser, and disabled gain control in S-param mode Design note: S-param branch syncs m_cached_input_ptr alongside m_cached_sparam_input to prevent the outer cache check from short-circuiting the S-param path after the first frame.
- _(ideal_filter)_Add S-parameter mode Add S-parameter mode to IdealFilterEngine, following the same pattern as AmplifierEngine (Task 2). When enabled, the filter applies S21 complex gain from a Touchstone file instead of the ideal passband response. - Add SParameterData fields, setSParamFilepath(), sparamMode(), etc. - Insert S-param branch in update() that applies |S21|^2 noise scaling and S21 complex gain to tones (no NF, no nonlinearity — filter is passive) - Update hoverSummary() to show S-Param Filter with point count - Add mode toggle + file browser to inspector panel in drawIdealFilterProperties - Disable ideal-mode filter controls when S-param mode is active - Link simulator::touchstone_parser in ideal_filter/CMakeLists.txt - Add tests/test_ideal_filter_sparam.cpp with 2 test cases - Register test file in tests/CMakeLists.txt
- _(equalizer)_Add EqualizerEngine with ideal gain-slope and S-param modes Create new EqualizerEngine component with: - Ideal mode: G(f) = G_ref + slope * log10(f/f_ref) frequency-dependent gain - S-param mode: load .sNp files, apply S21 complex gain (same pattern as amp/filter) - Node graph symbol: diagonal line with markers - Inspector panel: mode toggle, ref gain, ref frequency, slope controls, S-param file browser - Context menu: Add Equalizer in canvas - 4 unit tests: flat gain, slope, combined, S-param Part of s-param-rework (Task 5).

### Styling

- Address code-reviewer feedback (redundant cast, comment count, default switch guard, include order)

### Testing

- _(amplifier)_Add S-parameter mode tests
## [0.3.0] - 2026-06-21

### Bug Fixes

- _(coax)_Remove unused include, add explicit string include
- _(coax)_ASCII-safe test name for CTest on Windows
- _(coax)_Add Loss@fc readout to hoverSummary per spec
- Use no-arg inputPinId() for legacy engines in routing loop
- Add override to SplitterEngine::outputPinId(int)
- Zero-height title bar in expanded group rendering ImVec2(180, -16) made br_screen.y == tl_screen.y, producing an invisible title bar and an unclickable collapse button. Fixed to ImVec2(180, 8).
- Rebuild boundary pins after node deletion, add pin tooltips, batch cache rebuild
- Clear stale boundary pins on group expand to prevent imnodes assertion When a collapsed group's Expand button is clicked, setGroupCollapsed(false) must clear the boundary pins. Otherwise rebuildSynthMaps() still maps real pin IDs to synthetic boundary pin IDs, drawLinks() translates them, and ImNodes::Link() is called with unregistered attribute IDs — causing an assertion in EndNodeEditor (imnodes.cpp:118). Guard against this at both layers: - Engine: clear g.boundary_pins on expand (setGroupCollapsed) - Widget: skip expanded groups in rebuildSynthMaps() Also fix the position cache update in drawNodes: the old check (m_node_screen_positions.size() == 1) only matched on the very first frame, freezing the grid-to-screen offset forever. Use a per-frame first_visible flag instead so the offset tracks canvas panning.

### Documentation

- Add generic S-parameter component design spec
- Add generic S-parameter component implementation plan
- Add multi-port S-parameter component design spec
- Add multi-port SParamEngine implementation plan
- Add missing dox section to main agents file
- Add revised multi-port mode spec and implementation plan
- Add subcircuit groups design spec
- Add subcircuit groups implementation plan
- Tighten plan - fix unique IDs test in Task 7
- Imnodes spike report + plan updates for actual imnodes API - Spike confirms GetNodeGridSpacePos(int) (not GetNodeGridPos(int, ImVec2*)) - No ScreenToGrid/GridToScreen; use EditorContextGetPanning + screen-space rect - Phantom node workaround not needed; imnodes position pool is stable - Plan updated: rubber-band uses screen-space hit-test, gridToScreen calls replaced with manual panning transform, Task 14 becomes a no-op verification
- Fix groupsContainingNode signature in plan (const correctness) The plan's implementation used unordered_map::operator[] in a const member function, which does not compile. Fixed to use find() + static empty vector.
- Fix removeNode cascade algorithm in plan The plan's code incorrectly added the group's id to groups_to_remove whenever any member was removed, rather than first removing the node from the group's member list and then checking if the group should survive. Found by Task 8 implementer who correctly identified and fixed the issue during TDD.
- Document subcircuit groups in ARCHITECTURE.md
- Document subcircuit groups in README.md
- Mark subcircuit groups as completed in ROADMAP.md
- DOX pass for subcircuit groups

### Features

- _(coax)_Add CableSpec struct and MilTech preset table
- _(coax)_Add CoaxCableEngine with pass-through update
- _(coax)_Per-tone insertion loss with K1/K2 formula
- _(coax)_Per-bin noise scaling by inverse linear loss
- _(coax)_Per-tone and per-bin phase shift
- _(coax)_InspectorPanel properties for CoaxCable
- _(coax)_Add 'Add Coax Cable' to node graph context menu
- _(coax)_Wire onAddCoaxCable in RfSimulatorApp
- Unified SParamEngine replaces SParamFilter/SParamAmp
- Add some basic components for testing
- Add basic (1 setting) step attenuator
- Add multi-pin virtuals to IComponentEngine
- Add pin label vectors to GraphNode
- Render GraphNode pin labels in node widget
- Multi-port SParamEngine with dynamic pins and full matrix evaluation
- Route all input pins for multi-port S-param engines
- Inspector shows multi-port info for SParamEngine
- Add numInputPins/numOutputPins to IComponentEngine
- Add Splitter/Combiner modes with mode-aware pin layout
- Generic numInputPins routing, base inputPinId(int) forwards to inputPinId()
- Inspector shows mode/common port dropdowns for multi-port S-param
- Add Group and GroupBoundaryPin data types
- Add group collection and accessors to NodeGraphEngine
- Add addGroup with validation
- Add removeGroup
- Add renameGroup, setGroupCollapsed, isGroupCollapsed
- Add rebuildGroupBoundaryPins
- Cascade group cleanup in removeNode
- Add widget state for subcircuit groups
- Render expanded group backgrounds and title bars
- Render collapsed group as a single imnodes node
- Skip rendering internal nodes and internal links when group is collapsed
- Detect Shift+drag rubber-band on empty editor space
- Create Subcircuit popup with member list and name field
- Select group on group block click
- Inspector group panel
- Right-click context menu on groups
- Probe translation for boundary pins and group-block clicks
- Link creation through boundary pins with translation
- Link destruction with boundary pin rebuild
- Subcircuit groups - expandable/collapsible node groups with synthesized boundary pins

### Miscellaneous Tasks

- No-op — app wiring already supports multi-port SParamEngine
- Add common/include to common's INTERFACE include path Task 2 placed common/include/group.h but the existing common/CMakeLists.txt only exposed common/ (not common/include/) to consumers. Task 3's node_graph_engine.h will #include "group.h"; without this fix the build would fail with 'group.h: No such file or directory'. Reviewer flagged this in Task 2 review. Future headers in common/include/ (e.g. relocations) will also work with this path.

### Testing

- _(coax)_Connector loss, length clamping, freq clamping, zero-K preset
- _(coax)_Empty input, dirty flag, generation bump
- _(coax)_End-to-end gen → cable → amp chain
- Add multi-port SParamEngine tests with .s3p data
- New test case for sparam engine
- Integration tests for groups preserving signal flow and topology
- UI tests for subcircuit groups
- Update group test assertions and add expand/collapse UI regression test Groups default to collapsed now; update test_group.cpp assertions from collapsed==false to collapsed==true and adjust expand/collapse order. Add UI test that creates a subcircuit then clicks the Expand button to catch crashes on the expand transition (EndNodeEditor assertion from stale synthetic boundary pins, fixed in prior commit).

### Bench

- Add benchmarks for group operations

### Release

- V0.3.0
## [0.2.0] - 2026-06-15

### Bug Fixes

- _(sparam)_Table flags type, preserve vis on delete, add delete log
- _(sparam)_1-port default, empty file guard, log10 guard
- Use MHz consistently in pin tooltip freq display
- _(adc)_Stack params vertically in collapsible tree nodes
- _(adc)_Add units to parameter labels in widget
- _(adc)_Populate diagnostic FFT output as PSD in noise_W
- _(adc)_Sort FFT diagnostic frequency axis ascending
- _(adc)_Remove hard 1GHz upper limit on BW parameter
- _(adc)_Null-guard kiss_fft_alloc and D=1 edge cases
- _(core)_Enable 32-bit vertex indices for large ImPlot traces
- Properly brace sparam removal, unnest ADC loop in removeComponent
- Add onAddAdc to NodeGraphWidget context menu
- Noise jitter on individual probe traces, ctrl+click probes only
- Shift+click removes probe, ctrl+click adds
- Update spectrum widget sizing constraints
- Add pfd file dialog to S-parameter inspector panels
- Restrict PFB channelizer to ADC sources with explicit Fs
- IQ plot auto-scale y-axis to data range
- Guard ViewManager logging against null input pointers
- Cache input pointer address in dirty-flag check for correctness
- _(sa)_Use dedicated PFB colors independent of probe index palette
- _(pfb)_Use input noise directly in full spectrum output (fixes ~9 dB noise floor drop)
- _(pfb)_Reconstruct full-spectrum noise using per-bin channel weight sum (shows correct FFT gain)
- _(pfb)_Use flat per-channel noise density with effective noise bandwidth (eliminates filter scallop ripple)
- Resolve input pin probes to upstream source output
- Remove wayland dep for linux build
- Increase window size to default 1080p
- Dynamic INI buffer, deduplicate path, add amp NF/nonlinearity and SessionState tests - Replace fixed 256-byte stack buffer in SessionState::load() with dynamically-growing buffer (up to 32KB) to prevent silent truncation - Use SessionState::fileExists() in core.cpp instead of duplicating path derivation logic - Document PFB noise_W convention on full spectrum output (outputs[1]) - Add 5 tests for S-param amp NF, harmonics, IMD, compression - Add 7 tests for SessionState save/load round-trip and edge cases
- Derive PFB Fs_Hz from input spectrum grid
- Propagate ADC sample rate via Spectrum::fs_Hz
- Show PFB channelized output (outputs[0]) in spectrum analyzer
- PFB outputs[1] noise uses per-bin psd*weight^2, not channel-average
- Accumulate overlapping channel noise (sum not overwrite) for flat PFB band
- Flat PFB noise floor via overlap-averaged channel density
- Clamp m_active_channel when channel count reduced
- IQ plot persistence keys use PFB engine ID not array index
- Recompute channels on M change, extend max to 2048
- Use < not <= for tone DDC filter, NSD to noise_added_W
- Use per-spectrum frequency axis in spectrum widget
- Call update_dsp before draw_ui so first frame is valid
- Remove default S2P path from SParameterFilterEngine
- Use local graphs in benchmarks to avoid cross-case contamination
- Use size_t instead of int in buildDefaultFrequencyGrid
- BumpGeneration on splitter early-return paths
- Guard Win32 API calls in SessionState for cross-platform build
- Use graph outputPinId API, add cstdio header
- Gate <windows.h> includes with #ifdef _WIN32 for Linux build

### Documentation

- Add roadmap for future development
- Update roadmap — items 1 and 9 completed
- Add pfb channelizer reference
- Update roadmap — ADC sub-project complete
- Add inspector panel spec
- Update README with usage, architecture, and changelog
- Add banner image to README header
- Resize README banner to 70% width
- Center banner in readme
- Clean up readme
- Update README with new components, benchmarks, and performance features
- Expand build instructions and requirements in README
- Add reference, spec and plan for nonlinear amplifier model
- Update AGENTS.md — fix outdated typedef, add git guidelines
- Update README — fix test count, probe behavior, amp/PFB features
- Add benchmark table to README
- Clarify noise_W units in PFB outputs[1] accumulation
- Integrate dox
- Add MIT LICENSE
- Add CONTRIBUTING.md, slim AGENTS.md to pointer
- Add docs/README.md index for surviving resources
- _(readme)_Fix Windows build, add CI badge, link LICENSE

### Features

- Add drag interaction and update marker controls
- _(spectrum)_Add phase member to Tone and per-bin vectors
- Add mixer component with frequency conversion - MixerEngine: internal LO, produces sum + difference sidebands - Editable LO frequency (MHz) and conversion gain (dB) in widget - Noise scaled by linear conversion gain, phase preserved on sidebands - Integrated into app DSP loop, node graph, and removal logic - 4 test cases covering sidebands, phase, noise, and empty input - Design spec: docs/superpowers/specs/2026-04-30-mixer-design.md All 38 tests pass.
- Add Touchstone v1.0 parser for S-parameter files - New touchstone/ module with TouchstoneParser class - Parses option line (freq unit, parameter, format, reference Z) - Supports DB, MA, RI formats - Handles 1-port and 2-port v1.0 files - Port count inferred from .sNp extension - Frequencies normalized to Hz internally - Tested with real ADM-8344PSM .s2p (2650 pts, 10 MHz-26.5 GHz) - Synthetic tests for 1-port MA and 2-port DB formats - ROADMAP: feature #1 marked In Progress
- Add S-parameter amplifier with frequency-dependent gain - New s_parameter_amplifier/ module: engine + widget - Loads 2-port Touchstone .s2p on construction - Extracts |S21| vs frequency, linearly interpolates at runtime - Applies interpolated |S21| as gain (dB) to each tone - Scales noise density by |S21|² per frequency bin - Phase preserved from input (no phase rotation from S-params yet) - Gracefully handles missing/bad files (logs warning, acts as unity gain) - Integrated into app: node graph, DSP loop, widget panel, removal - Tests: real .s2p gain at 1 GHz, interpolation, out-of-band clamp, bad file - 47/47 tests pass
- Add |S21| visualization to S-parameter amplifier widget - SParameterAmplifierEngine exposes s21Freqs() and s21Mag() getters - Widget draws ImPlot showing |S21| in dB vs frequency (GHz) - Each loaded amplifier rendered as separate line with legend - Plot only shown when at least one S-param amp is loaded - Links implot target in CMakeLists
- _(sparam)_N-port storage, reload, phase rotation
- _(sparam)_N-port storage, reload, phase rotation
- _(sparam)_File browse, forward param selector, param plot toggles
- Add pin tooltips showing signal summary in node graph
- Add IQStream type for digital time-domain signals
- _(adc)_Add engine header with 8 params and IQStream output
- _(adc)_Implement utilities, tone synthesis, and noise generation
- _(adc)_Implement DDC, LPF, decimation, and diagnostic FFT
- _(adc)_Add widget with 8-parameter table
- _(adc)_Wire ADC into app with graph + DSP + UI
- _(docs)_Add RF ADC reference description
- _(adc)_Replace custom FFT with KissFFT + Hann window
- RF ADC component (digital chain sub-project 1)
- Add hoverSummary() to all engine types
- Add node hover tooltip infrastructure
- Add InspectorPanel class with per-type property drawers
- Replace per-type widget windows with Inspector Panel
- Add component-type vector symbols to graph nodes
- Extend freq range to +/-20GHz, enforce min widget height
- Multi-probe API on NodeGraphEngine (up to 4)
- Multi-probe pin colors with per-slot palette
- Multi-trace spectrum display with per-probe colors
- Add noise figure to mixer model
- Add version and git hash to UI
- Extract SParameterData helper and add S-parameter filter component
- Add drag-to-zoom on spectrum plot with reset
- Add pfb channelizer module skeleton
- Implement pfb channelizer engine DSP core
- Wire pfb channelizer into app layer
- Add frequency-domain PFB channelizer component
- Add IQ time-domain plot via kissFFT IDFT on PFB channel output
- Add x-axis drag-to-zoom with reset on IQ time plot
- Add generation counter to Spectrum for dirty tracking
- Change SignalNode inputs to const Spectrum pointers
- Wire SignalNode inputs as pointers in app layer
- Add dirty-flag short-circuit to SignalGeneratorEngine
- Add dirty-flag and pointer inputs to AmplifierEngine
- Add dirty-flag and pointer inputs to MixerEngine
- Add dirty-flag and pointer inputs to SplitterEngine
- Add dirty-flag and pointer inputs to AdcEngine
- Add dirty-flag and pointer inputs to SParameterAmplifierEngine
- Add dirty-flag and pointer inputs to SParameterFilterEngine
- Add dirty-flag and pointer inputs to PFBChannelizerEngine
- Add generation-keyed trace cache to spectrum analyzer widget Individual traces and combined spectrum are now cached per (Spectrum* + generation + RBW + VBW + jitter) key. Eliminates unnecessary RBW reconvolution on every frame for static scenes. Also adds const getters for jitter settings on SpectrumAnalyzerEngine.
- Cache PFB channel maps to avoid recompute on every frame
- Add topological sort and PFB channel map caching - NodeGraphEngine: add nodeIdForPin() and topologicalOrder() (Kahn's algorithm) for correct DAG evaluation order - App: replace hardcoded component update order with graph-derived topological traversal via a node_id -> update function map - PFB: cache frequency grid + Fs to skip recomputeChannels when grid hasn't changed (common case) - Skip SpectrumPool -- pointer-based wiring from branch 1 already eliminated the heap churn a pool would address
- Add IconRegistry module for pixel art node icons Creates a new icon_registry module with OpenGL texture loader wrapping stb_image and IconRegistry class mapping component prefix to texture. NodeGraphWidget updated to display icons via ImGui::Image; falls back to empty dummy area when no icon is loaded (no vector shape fallback).
- Add nonlinear model coefficients to AmplifierEngine (OIP2/OIP3)
- _(amplifier)_Add nonlinear processing pipeline (harmonics, IMD, compression) Harmonics (2nd/3rd), IM2/IM3 intermodulation from tone pairs, and power compression computed from total distortion power relative to fundamental. Gated by m_enable_nonlinear flag (defaults to off).
- _(ui)_Add amplifier nonlinear controls (checkbox, OIP2/OIP3, P1dB estimate)
- _(pfb)_Add second output with full spectrum and active-channel query API
- _(sa)_Add PFB dual-trace rendering (full spectrum + active channel highlight)
- _(app)_Wire PFB to spectrum analyzer for dual-trace display
- _(sparam-amp)_Add noise figure and nonlinearity (OIP2/OIP3) with frequency-dependent gain
- Add SessionState class for INI persistence
- Add window visibility flags and SessionState to RfSimulatorApp
- Load/save window visibility via SessionState, pass p_open
- Add View toggles in Inspector Panel for window visibility
- Set default docking layout on first run
- Change spectrum widget to vector of PFB pointers
- Change inspector panel to vector of PFB pointers with combo selector
- Vector-ify PFB, IQ plot, and visibility members in app
- Implement DDC with NCO=-Fs/4 and decimate-by-2 in ADC
- Add sample rate to generator for PFB chain support
- Add ideal filter module skeleton
- Add Ideal Filter to node graph context menu
- Wire IdealFilterEngine into app lifecycle
- _(inspector)_Wire ideal filter into inspector panel Add IdealFilter to ComponentType enum, findSelected, label and properties switches, and drawIdealFilterProperties method with filter type/cutoff controls and Measure toggle.
- Add IComponentEngine polymorphic base
- Add ComponentRegistry class
- Add PFB channelizer widget header and CMake target
- Implement PFB channelizer grid widget
- Wire PFB channelizer grid widget into app

### Miscellaneous Tasks

- Add portable-file-dialogs for native file browser
- Add adc module with engine + widget targets
- Remove unused KissFFT dependency
- Ignore graphify knowledge graph
- Clean up tracked pollution files and update gitignore
- Ignore .worktrees directory
- Add GitHub workflows, issue/PR templates
- Add doc filters to prevent unnecessary CI triggers
- Add Windows MinGW-w64 matrix, install g++-14
- Run on v* tag push
- Install libgl1-mesa-dev to fix OpenGL find_package failure
- Change version string formatting
- Install cmake and pkg-config for portability
- Gitignore .actrc (developer-local Docker socket config)
- Exclude catch_discover_tests concatenation quirk on Windows
- Bump actions/checkout to v6 for Node.js 24 compat
- Only run on tag push, not every master push

### Performance

- _(adc)_Dirty-flag skipping, FFT, lower default N_samples
- Precompute PFB lookup map to avoid O(N*M) per frame
- Cache kissFFT config to avoid per-frame allocation in IQ plot

### Refactoring

- _(node_graph)_Vectorize SignalNode and GraphNode for multi-port support
- _(sparam)_Update engine header for N-port data model
- _(sparam)_Update engine header for N-port data model
- _(adc)_Remove BW param, compute LPF cutoff from Fs/(2D)
- _(adc)_Pure frequency-domain processing, remove time-domain pipeline
- _(adc)_Remove f_channel param, ADC is pure sampler+aliaser
- Deduplicate pin lookups, grid builder, and signal wiring; remove dead code
- Move RBW cache into engine to preserve noise jitter animation Widget-level trace caching bypassed renderSpectrum entirely, freezing the noise jitter animation. Instead, renderSpectrum now internally caches the expensive integrate+applyRBW step keyed by (spectrum*, generation, RBW, bin_width). The dBm conversion, noise jitter, and VBW smoothing still run every frame so the spectrum display looks like a real instrument. Widget header cleaned up: removed TraceCacheEntry, CombinedCacheKey, and associated helper declarations that were part of the reverted widget-level cache.
- Remove redundant ComponentType::PFB guard
- Fold PFB update wiring into addWiredUpdate
- Extract NonlinearModel class from amplifier and S-param amp
- Engines inherit IComponentEngine base
- App uses ComponentRegistry for engine lifecycle

### Testing

- _(sparam)_Update tests for N-port engine API
- _(sparam)_Tighten gain bound, document magic numbers
- _(adc)_Add 6 test cases for ADC engine
- Add pfb channelizer engine tests
- Update test suites for const Spectrum pointer inputs
- Add DSP micro-benchmarks for all engine types Benchmarks cover dirty vs clean (cached) update cycles for generator, amplifier, mixer, splitter, PFB channelizer, and spectrum analyzer. Key result: renderSpectrum RBW cache saves ~37 us per call (185 vs 148 us) on a 401-bin grid. Engine-level dirty flags show trivial overhead (~5 ns) for the cached path. Run: ./build/bin/tests.exe [bench]
- Add 5 nonlinear amplifier tests (passthrough, harmonics, IMD, compression, no-IMD)
- _(pfb)_Add tests for second output and active channel query API
- Add flat noise floor overlap-boundary regression test
- Rewrite ADC tests for DDC + decimation-by-2
- Fix makeInput UB, tighten grid assertion, validate NSD level
- Add ideal filter engine tests
- Add ComponentRegistry unit tests
