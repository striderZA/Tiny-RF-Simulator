# Component Registration Unification + App Decomposition

**Issue:** https://github.com/striderZA/Tiny-RF-Simulator/issues/51

**Problem:** Adding a new RF component (the project's headline extensibility claim) requires
coordinated edits across ~10 files because six parallel hardcoded type-dispatch tables drift.
`app/src/app.cpp` is a 1320-line god-object, and the PFB widget lockstep-vector lifecycle has
already caused one use-after-free (issue #37) and one missing-`markDirty()` bug (Equalizer).

## Goal

Adding a component should touch **one registry row + one engine module** (+ unavoidable per-kind
view code in `node_graph`). All type dispatch — canvas menu, add, duplicate, save/load, inspector
drawing — flows through a single `ComponentTypeRegistry` table in `app/`.

## Scope

Three workstreams, staged as separate implementation phases:

1. **Unified registration object + `type_name()` virtual** — extend `ComponentTypeRegistry` into
   the single table; add `type_name()` to `IComponentEngine`; rewire save/load/duplicate/menu/
   inspector dispatch through it. Fixes the Equalizer `markDirty()` bug.
2. **`PFBViewManager` extraction** — move the four lockstep PFB widget vectors and their six
   rebuild sites into one owning class.
3. **`ProjectSerializer` extraction** — move `saveProject()`/`loadProject()`/`newProject()` JSON
   logic out of `RfSimulatorApp`.

Out of scope: removing the test-only accessors (`testGraphEngine()`, `testComponents()`, ...) —
they are a smell but removing them is a separate cleanup that would churn `test_project_file.cpp`
with no behavior win. `NodeKind` enum + `themeColor` + `drawSchematicSymbol` stay in `node_graph`
(view-layer: a new schematic symbol is inherently per-kind drawing code). The library-authoring
form's type combo is driven from the registry instead of a hardcoded `type_names[]` array.

## Backward compatibility constraints (non-negotiable)

- `.rfsim` project files keep using today's type strings: `SignalGenerator`, `Amplifier`,
  `Splitter`, `Mixer`, `Attenuator`, `Combiner`, `Equalizer`, `ADC`, `PFBChannelizer`, `CoaxCable`,
  `IdealFilter`. `saveProject` writes them; `loadProject` must accept **both** legacy and canonical
  (`amplifier`, ...) names. Old project files load unchanged; new files are indistinguishable from
  old ones for the type field.
- Library JSON (`component_data/library/**/*.json`, `rf-sim-libraries/**`) keeps lowercase type
  strings: `amplifier`, `attenuator`, `splitter`, `filter`, `mixer`, `equalizer`, `combiner`, `adc`.
- Canvas menu labels stay byte-identical: `Add Generator`, `Add Amplifier`, `Add Splitter`,
  `Add Combiner`, `Add Coax Cable`, `Add Equalizer`, `Add Mixer`, `Add RF ADC`,
  `Add PFB Channelizer`, `Add Ideal Filter`, `Add Attenuator`. UI tests in
  `test_engine/ui_tests.cpp` click these strings.

---

## Phase 1 — Unified registration object + `type_name()` virtual

### 1.1 `IComponentEngine` gains `type_name()`

`common/component_interface.h`:

```cpp
virtual std::string_view type_name() const = 0;
```

Pure virtual (no default) — a new engine **must** self-identify or it won't compile. Returns the
canonical lowercase key, e.g. `"amplifier"`. Implemented in all 11 engine headers (inline, e.g.
`std::string_view type_name() const override { return "amplifier"; }`) plus the two test engines in
`tests/test_component_registry.cpp`.

### 1.2 `ComponentTypeDescriptor` becomes the single table

`app/include/component_type_registry.h` — extend the existing struct:

```cpp
struct ComponentTypeDescriptor {
    std::string type;         // canonical key, e.g. "amplifier"
    std::string project_type; // .rfsim name, e.g. "Amplifier"
    std::string display_name; // e.g. "Amplifier"
    std::string menu_label;   // e.g. "Add Amplifier"
    std::string label_prefix; // graph label prefix, e.g. "Amplifier " (trailing space)
    NodeKind kind;            // NodeKind::Amplifier (node_graph include)
    bool authorable = false;  // appears in New Component form combo
    bool supports_sparam_file = false;
    std::vector<ParameterField> fields;
    std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int)> create;
    std::function<void(InspectorPanel &, IComponentEngine &)> draw_inspector;
};
```

Notes:
- `create` replaces the old params-taking `factory`. Parameters are applied by the caller via
  `deserialize()` — params application now lives in exactly one place per engine (its
  `deserialize()`), not duplicated between registry factories and `loadProject` branches.
- `draw_inspector` is populated at app startup by one registration function in
  `inspector_panel.cpp`, e.g. `void registerInspectorDrawers(ComponentTypeRegistry &registry);`
  called from the `RfSimulatorApp` constructor after the registry is built; it assigns
  `draw_inspector` on each descriptor. The lambda does the `static_cast` + call, e.g.
  `[](InspectorPanel &p, IComponentEngine &e) { p.drawAmplifierProperties(static_cast<AmplifierEngine &>(e), e.id()); }`.
  `InspectorPanel::draw()` only invokes `desc->draw_inspector`. The existing `drawXProperties`
  methods become public (single-file churn, no behavior change).
- `ComponentTypeRegistry` keeps `find(std::string_view)` (canonical `type` lookup, existing) and
  gains `findByProjectType(std::string_view)` (accepts canonical + legacy `.rfsim` names);
  `all()` unchanged.
- Registry now holds **11** descriptors: the current 8 (`amplifier`, `attenuator`, `splitter`,
  `filter`→`IdealFilter`, `mixer`, `equalizer`, `combiner`, `adc`) plus `generator`, `coax`,
  `pfb`. `authorable` is true only for the current 8 (generator/coax/pfb stay out of the library
  authoring form).

### 1.3 `saveProject` / `loadProject` / `duplicateComponent`

- `saveProject`: replace `s_type_names` static map with
  `registry.find(comp->type_name())->project_type`.
- `loadProject`: replace the 11-branch chain with
  `auto *d = registry.findByProjectType(type); if (!d) { LOG_WARN(...); ... } auto *comp = d->create(m_components, m_graph_engine, m_next_component_id++); comp->deserialize(params);`.
  PFB restore calls `m_pfb_views.addFor(...)` (see Phase 2).
- `duplicateComponent`: replace the 11-way `dynamic_cast` chain + `dup` lambda with
  `auto *d = registry.find(src->type_name()); auto *copy = d->create(...); copy->deserialize(src->serialize());`
  plus the existing position-offset and part-number copy. PFB duplicate calls `m_pfb_views.addFor(...)`.
- `ComponentLibrary::instantiate` (`app/src/component_library.cpp`): `create()` +
  `deserialize(def.parameters)`; the `def.type == "amplifier"` S-param special case stays.

### 1.4 Canvas menu + add path

`node_graph/include/node_graph_widget.h`:

```cpp
struct AddableComponent {
    std::string menu_label;
    std::function<void(ImVec2)> on_add;
};
void setAddableComponents(std::vector<AddableComponent> addable);
```

Remove the 11 `onAdd*` callbacks. `handleContextMenu()` iterates `m_addable_components`, rendering
one `ImGui::MenuItem(menu_label)` per entry. App constructor builds the list from
`ComponentTypeRegistry::instance().all()` and one shared `addComponent(const ComponentTypeDescriptor *, ImVec2)`
method that: `create()`s, sets node position, calls `markDirty()` **unconditionally** (fixes
Equalizer bug), and routes PFB through `m_pfb_views.addFor(...)`.

### 1.5 Inspector dispatch

`inspector_panel.cpp`:
- `findSelected()`: `engine = m_components->find(selected_id)` then
  `auto *d = ComponentTypeRegistry::instance().find(engine->type_name()); return {d, engine};`
  — the 11-way `dynamic_cast` chain dies. `Hit` becomes
  `{ const ComponentTypeDescriptor *desc; IComponentEngine *engine; }`; the private `ComponentType`
  enum is deleted.
- `labelForHit()`: derive from `desc->display_name` (+ `id`), PFB special case kept. Fixes a
  latent drift bug: today's `labelForHit` switch has no `CoaxCable`/`Equalizer` case, so selecting
  those components shows an empty panel title (they fall through to `default`).
- `draw()`: after the group/PFB-selector handling, dispatch via
  `if (hit.desc && hit.desc->draw_inspector) hit.desc->draw_inspector(*this, *hit.engine);` — the
  type switch dies.

### 1.6 Node graph kind mapping becomes data-driven

`node_graph` keeps `NodeKind`, `themeColor`, `drawSchematicSymbol` (per-kind view code stays
there). But `nodeKindFromLabel`'s 11-branch chain is replaced by a widget-held prefix map:

```cpp
void NodeGraphWidget::registerNodeKind(std::string label_prefix, NodeKind kind);
```

`drawNodes()` looks up `node.label` prefix → `NodeKind` → `themeColor(kind)`. The app constructor
feeds `registerNodeKind(d->label_prefix, d->kind)` for each descriptor. `nodeKindFromLabel` is
deleted from `node_graph_engine.h`; the `[node_graph][appearance]` tests that asserted its exact
prefix list are removed (replaced by a registry-completeness test asserting every descriptor's
`label_prefix` maps to its `kind`).

### 1.7 New Component form combo

`app/src/app.cpp::drawComponentFormModal()`: replace the hardcoded `type_names[]` array with the
registry's `authorable` descriptors.

---

## Phase 2 — PFBViewManager extraction

New `app/include/pfb_view_manager.h` + `app/src/pfb_view_manager.cpp`:

```cpp
class PFBViewManager {
  public:
    void addFor(PFBChannelizerEngine &engine, SessionState &state);       // IQ + grid widgets
    void rebuild(const ComponentRegistry &components, SessionState &state); // sync all PFBs
    void clear();
    void draw();                                                          // draw_ui loop body
    void saveVisibility(SessionState &state) const;                       // destructor path
    std::vector<bool> &iqVisibility() { return m_show_iq_pfbs; }          // InspectorPanel hooks
    std::vector<bool> &gridVisibility() { return m_show_pfb_grids; }

  private:
    std::vector<std::unique_ptr<IQPlotWidget>> m_iq_widgets;
    std::vector<bool> m_show_iq_pfbs;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> m_pfb_grid_widgets;
    std::vector<bool> m_show_pfb_grids;
};
```

- `RfSimulatorApp` members `m_iq_widgets` / `m_show_iq_pfbs` / `m_pfb_grid_widgets` /
  `m_show_pfb_grids` are deleted; one `PFBViewManager m_pfb_views;` member replaces them.
- The six rebuild sites (`onAddPFB`, `onRemoveNode`, `duplicateComponent`, `loadProject`,
  `newProject`, destructor) collapse into `addFor` / `rebuild` / `clear` / `saveVisibility` calls.
- `draw_ui()`'s IQ-plot and channelizer-grid loops become `m_pfb_views.draw();`.
- `InspectorPanel::setPFBWindowVisibility(&m_show_iq_pfbs, &m_show_pfb_grids)` call site becomes
  `setPFBWindowVisibility(&m_pfb_views.iqVisibility(), &m_pfb_views.gridVisibility())`.
- **Member declaration order**: `PFBViewManager` is declared after `m_components` in `app.h`
  (reverse-declaration destruction ⇒ manager/widgets destroyed before the engines they reference).
  This is strictly safer than today's order, though not load-bearing: `IQPlotWidget::~IQPlotWidget`
  only frees `m_ifft` and `PFBChannelizerWidget` has no custom destructor, so no destructor
  dereferences a dead engine either way.

---

## Phase 3 — ProjectSerializer extraction

New `app/include/project_serializer.h` + `app/src/project_serializer.cpp`:

```cpp
class ProjectSerializer {
  public:
    ProjectSerializer(ComponentRegistry &components, NodeGraphEngine &graph,
                      NodeGraphWidget &graph_widget, PFBViewManager &pfb_views,
                      SessionState &state, int &next_component_id);
    void save(const std::string &path);
    bool load(const std::string &path);   // returns false on parse/unknown-type errors (logged)
    void reset();                         // newProject: links, components, probes, counters, PFBs
  private:
    // all the JSON/link/pin/position/group logic currently in app.cpp
};
```

- `RfSimulatorApp::saveProject` / `loadProject` / `newProject` become thin wrappers that delegate
  JSON + graph/component work to `m_serializer` and keep only app-level concerns: file dialogs,
  `m_dirty` flag, `refreshExtensions()`, `m_current_project_path`, window-state persistence.
- `loadProject`'s `LOG_WARN` unknown-type path, position restore, part-number restore, link
  restore (component-index+port pairs), group restore, probe restore, and counter resets all move
  with the logic.

---

## Files changed (summary)

**Phase 1**
- `common/component_interface.h` — `type_name()` pure virtual
- 11 engine headers (`signal_generator`, `amplifier`, `splitter`, `mixer`, `adc`,
  `pfb_channelizer`, `coax`, `equalizer`, `ideal_filter`, `attenuator`, `combiner`) — inline
  `type_name()` override
- `app/include/component_type_registry.h` / `app/src/component_type_registry.cpp` — descriptor
  fields, 11 rows, new lookups, `create`
- `app/include/component_library.h` / `app/src/component_library.cpp` — `create`+`deserialize`
- `app/include/app.h` / `app/src/app.cpp` — `addComponent`, menu list build, `drawComponentFormModal`
- `app/include/inspector_panel.h` / `app/src/inspector_panel.cpp` — registry-driven dispatch,
  draw registration
- `node_graph/include/node_graph_engine.h` — delete `nodeKindFromLabel`
- `node_graph/include/node_graph_widget.h` / `node_graph/src/node_graph_widget.cpp` —
  `setAddableComponents`, `registerNodeKind`, menu loop
- `tests/test_component_registry.cpp` — test engines gain `type_name()`
- `tests/test_node_graph_engine.cpp` — drop `nodeKindFromLabel` cases

**Phase 2**
- `app/include/pfb_view_manager.h` / `app/src/pfb_view_manager.cpp` — new
- `app/include/app.h` / `app/src/app.cpp` — member swap, 6 call sites
- `app/CMakeLists.txt` — add new sources

**Phase 3**
- `app/include/project_serializer.h` / `app/src/project_serializer.cpp` — new
- `app/include/app.h` / `app/src/app.cpp` — thin wrappers
- `app/CMakeLists.txt` — add new sources

**Tests + docs**
- New standalone test executable `tests/test_component_dispatch.cpp` (per `tests/AGENTS.md` MinGW
  ceiling: standalone `add_executable`, not a new file in the `tests` target)
- `app/AGENTS.md`, `common/AGENTS.md` — DOX pass (new owners, `type_name()` contract)
- `openwiki/testing/guidance.md` — test file table update

## Testing

- **Registry completeness** (new standalone exe): all 11 canonical types present; every descriptor
  has non-empty `type`, `project_type`, `menu_label`, `label_prefix`, `kind`, `create`,
  `draw_inspector`; every `create()` returns an engine whose `type_name()` matches the row; every
  descriptor's `label_prefix` maps to its `kind`.
- **Round-trip** (new standalone exe, ImGui fixture like `test_project_file.cpp`): for each of the
  11 types — add via `addComponent`, serialize, reload, assert type/params survive. PFB reload
  recreates IQ/grid widgets.
- **Equalizer dirty-flag regression** (new standalone exe): `addComponent(equalizer)` →
  `app.isDirty() == true` (was the bug).
- **Backward compat**: hand-written `.rfsim` containing legacy `SignalGenerator`/`ADC`/
  `IdealFilter` strings loads; `component_data/library` scan still validates (`filter` ↔
  `IdealFilter` mapping intact).
- Existing `build/bin/tests.exe`, `test_issue37_pfb_input_removal`, `test_extensions`,
  `test_component_authoring`, `test_signal_domain`, `ui_tests` all pass unchanged (menu labels and
  `.rfsim` strings identical).

## Risks

- **`draw_inspector` access**: `InspectorPanel::drawXProperties` are private today. Making them
  public or registering via a friend/free-function seam is the main interface churn — contained in
  one file, no behavior change.
- **Registry growth**: `component_type_registry.cpp` grows to 11 rows (~+250 lines) but stays a
  flat declarative table — the point of the refactor.
- **Ordering**: `PFBViewManager` member placement must keep widget-before-engine destruction
  order; verified by existing `test_issue37_pfb_input_removal` (no ASan hits).
