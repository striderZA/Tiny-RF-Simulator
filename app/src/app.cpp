#include "app.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "graph_link_policy.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "pfb_channelizer_engine.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <portable-file-dialogs.h>
#include <unordered_map>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif
// Keep only filesystem-safe characters for path segments: [A-Za-z0-9-_ ].
// Strips everything else (incl. /, \\, and . which eliminates .. risks).
// Trims leading/trailing spaces. Returns fallback if result is empty.
static std::string sanitizePathSegment(const std::string &s, const std::string &fallback) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
            out.push_back(c);
    }
    size_t start = out.find_first_not_of(' ');
    if (start == std::string::npos)
        return fallback;
    size_t end = out.find_last_not_of(' ');
    out = out.substr(start, end - start + 1);
    return out.empty() ? fallback : out;
}

// Directory of the running executable, for exe-relative data/layout lookup.
// Falls back to the current working directory if exe-path detection fails.
// Same convention as layout/ (LayoutManager) and tutorial/ (TutorialState).
static std::string appExeDir() {
    std::string exe_path;
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        exe_path = buf;
#elif defined(__APPLE__)
    char buf[PATH_MAX] = {};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        exe_path = buf;
#else
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        exe_path = buf;
    }
#endif
    if (exe_path.empty())
        return std::filesystem::current_path().string();
    std::filesystem::path parent = std::filesystem::path(exe_path).parent_path();
    if (parent.empty())
        return std::filesystem::current_path().string();
    return parent.string();
}

RfSimulatorApp::RfSimulatorApp() : m_components(m_graph_engine, m_view_manager) {
    m_graph_widget = std::make_unique<NodeGraphWidget>(m_graph_engine);
    m_serializer = std::make_unique<ProjectSerializer>(
        m_components, m_graph_engine, *m_graph_widget, m_pfb_views, m_state, m_next_component_id,
        m_show_log, m_show_spectrum, m_show_properties, m_show_node_editor, m_na_engine);

    std::vector<NodeGraphWidget::AddableComponent> addable;
    for (const auto *desc : ComponentTypeRegistry::instance().all()) {
        addable.push_back(
            {desc->menu_label, [this, desc](ImVec2 pos) { addComponent(desc, pos); }});
        m_graph_widget->registerNodeKind(desc->label_prefix, desc->kind);
    }
    m_graph_widget->setAddableComponents(std::move(addable));
    m_graph_widget->onNodeMoved = [this]() { markDirty(); };
    m_graph_widget->onLinkChanged = [this]() { markDirty(); };
    m_graph_widget->onLinkCreating = [this](int start_pin, int end_pin) {
        const int target_node_id = m_graph_engine.nodeIdForPin(end_pin);
        auto *target = m_components.find(target_node_id);
        const int source_node_id = m_graph_engine.nodeIdForPin(start_pin);
        auto *source = m_components.find(source_node_id);
        return graphLinkAllowed(source, target, start_pin, end_pin);
    };
    m_graph_widget->onRemoveNode = [this](int id) {
        markDirty();
        m_components.remove(id);
        // Immediately re-wire remaining components' inputs against the current graph
        // topology. Node removal only strips graph links/pin bookkeeping; it does not
        // touch SignalNode::inputs pointers held by surviving components, and the next
        // scheduled rewire (update_dsp(), start of next frame) hasn't run yet. Any
        // component downstream of the removed node still holds a raw Spectrum* into the
        // just-destroyed engine's SignalNode. Widgets that dereference node().inputs[]
        // directly during this same draw_ui() call (e.g. PFBChannelizerWidget::draw() /
        // rebuildCache(), InspectorPanel) would otherwise use-after-free. See issue #37.
        rewireInputs();
        m_pfb_views.rebuild(m_components, m_state);
    };
    m_graph_widget->onNodeHover = [this](int id) { return m_components.hoverSummary(id); };
    m_graph_widget->onDuplicateNode = [this](int id) { duplicateComponent(id); };

    m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine)
        .addTone(100e6, -20.0);
    m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);

    m_inspector_panel = std::make_unique<InspectorPanel>(m_graph_engine, m_components);
    m_inspector_panel->registerDrawers(ComponentTypeRegistry::instance());
    m_inspector_panel->onRemoveNode = [this](int graph_node_id) {
        if (m_graph_widget->onRemoveNode)
            m_graph_widget->onRemoveNode(graph_node_id);
    };

    m_inspector_panel->setViewToggles({&m_show_log, &m_show_spectrum, &m_show_properties,
                                       nullptr, // iq_plot (per-PFB toggles used instead)
                                       &m_show_node_editor});
    m_inspector_panel->onParamChange = [this]() { markDirty(); };

    refreshExtensions();

    m_library_browser = std::make_unique<LibraryBrowserWidget>(m_library);
    m_library_browser->onInsert = [this](const ComponentDefinition &def) {
        auto *engine =
            m_library.instantiate(def, m_next_component_id++, m_components, m_graph_engine);
        if (engine)
            markDirty();
    };

    m_library_browser->onNewComponent = [this]() { openNewComponentForm("amplifier"); };
    m_library_browser->onEditComponent = [this](const ComponentDefinition &def) {
        openEditComponentForm(def);
    };

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine, m_view_manager);
    m_na_widget = std::make_unique<NetworkAnalyzerWidget>(m_na_engine, m_graph_engine);
    // Sweep-param/Point A/B edits in the Network Analyzer panel are project
    // state (persisted by ProjectSerializer) — mark the project dirty exactly
    // like InspectorPanel::onParamChange does for component params.
    m_na_widget->onParamChange = [this]() { markDirty(); };

    // Ensure all engine nodes are registered with the widget's imnodes context
    // so saveProject() can read node positions (GetNodeEditorSpacePos) without
    // crashing even before the first render frame.
    m_graph_widget->syncNodesFromEngine();

    load_window_states();

    // Offer the guided walkthrough once, on the first launch of a given build.
    // Tutorial visibility itself is transient session state, so it is not
    // persisted through SessionState — only the completion marker is durable.
    m_show_tutorial_first_run_prompt = !m_tutorial_state.completed();
}

// --- Network Analyzer host adapter -----------------------------------------
// RfSimulatorApp implements the engine's injected lookups (see app.h and
// network_analyzer_engine.h's layering comment): componentForNode wraps
// ComponentRegistry::find; beginScratchPass hands out one throwaway scratch
// graph+registry per measurement pass, destroyed (RAII) at pass end so the
// real graph/registry are never touched by the clone-chain measurement.

RfSimulatorApp::NaHost::NaHost(ComponentRegistry &components) : m_components(components) {}

IComponentEngine *RfSimulatorApp::NaHost::componentForNode(int graph_node_id) const {
    return m_components.find(graph_node_id);
}

std::unique_ptr<INetworkAnalyzerScratch> RfSimulatorApp::NaHost::beginScratchPass() const {
    return std::make_unique<RfSimulatorApp::NaScratch>();
}

RfSimulatorApp::NaScratch::NaScratch() : m_registry(m_graph, m_view) {}

IComponentEngine *RfSimulatorApp::NaScratch::createClone(std::string_view type, int id) {
    const auto *desc = ComponentTypeRegistry::instance().find(type);
    if (!desc)
        return nullptr;
    return desc->create(m_registry, m_graph, id);
}

void RfSimulatorApp::addComponent(const ComponentTypeDescriptor *desc, ImVec2 pos) {
    IComponentEngine *comp = desc->create(m_components, m_graph_engine, m_next_component_id++);
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), pos);
    if (desc->type == "pfb") {
        m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(comp), m_state);
    }
    markDirty(); // unconditional — fixes the Equalizer missing-markDirty bug
}

void RfSimulatorApp::load_window_states() {
    m_show_log = m_state.loadBool("WindowState", "Log", true);
    m_show_spectrum = m_state.loadBool("WindowState", "SpectrumAnalyzer", true);
    m_show_na = m_state.loadBool("WindowState", "NetworkAnalyzer", false);
    m_show_properties = m_state.loadBool("WindowState", "Properties", true);
    m_show_node_editor = m_state.loadBool("WindowState", "NodeEditor", true);
    m_show_help = m_state.loadBool("WindowState", "Help", false);
}

void RfSimulatorApp::duplicateComponent(int graph_node_id) {
    IComponentEngine *src = m_components.find(graph_node_id);
    if (!src)
        return;

    // Capture source position before creating the new node
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImVec2 src_pos = ImNodes::GetNodeEditorSpacePos(graph_node_id);
    constexpr float OFFSET = 40.0f;

    // Capture source part number for copying to the duplicate
    std::string src_part_number;
    for (const auto &gn : m_graph_engine.nodes()) {
        if (gn.node_id == graph_node_id) {
            src_part_number = gn.part_number;
            break;
        }
    }

    // Clone via the registry: create a default engine, then copy params through
    // serialize/deserialize. Removes the 11-way dynamic_cast chain.
    const auto *desc = ComponentTypeRegistry::instance().find(src->type_name());
    if (!desc)
        return;
    IComponentEngine *copy = desc->create(m_components, m_graph_engine, m_next_component_id++);
    copy->deserialize(src->serialize());
    int new_nid = copy->graphNodeId();
    // Register with imnodes pool and set position
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImNodes::SetNodeEditorSpacePos(new_nid, ImVec2(src_pos.x + OFFSET, src_pos.y + OFFSET));
    // Copy library part number
    if (!src_part_number.empty())
        m_graph_engine.setNodePartNumber(new_nid, src_part_number);
    // PFB also needs IQ plot widget and grid widget (same as addComponent)
    if (desc->type == "pfb") {
        m_pfb_views.addFor(*static_cast<PFBChannelizerEngine *>(copy), m_state);
    }

    markDirty();
}

void RfSimulatorApp::newProject() {
    m_serializer->reset();
    m_spectrum_widget->setProbeLabels({});
    m_current_project_path.clear();
    refreshExtensions();
    m_dirty = false;
}

void RfSimulatorApp::requestTutorial() {
    // Same guard New/Open/Exit use — startTutorial() discards the current
    // project, so the user must get the chance to save first.
    if (m_dirty) {
        m_pending_action = PendingAction::Tutorial;
        m_show_unsaved_dialog = true;
    } else
        startTutorial();
}

void RfSimulatorApp::startTutorial() {
    // Reset to the same Generator + Amplifier pair the app seeds on first launch,
    // so every step's instruction matches what the user is actually looking at.
    newProject();
    m_components.add<SignalGeneratorEngine>(m_next_component_id++, m_graph_engine)
        .addTone(100e6, -20.0);
    m_components.add<AmplifierEngine>(m_next_component_id++, m_graph_engine);
    m_graph_widget->syncNodesFromEngine();

    // Every panel a step highlights must be on screen for the highlight to
    // resolve — the Component Library in particular is hidden by default.
    m_show_node_editor = true;
    m_show_properties = true;
    m_show_spectrum = true;
    m_show_library = true;

    m_tutorial_state.start();
    m_show_tutorial = true;
}

void RfSimulatorApp::testMakeDirty() { markDirty(); }

void RfSimulatorApp::markDirty() { m_dirty = true; }
void RfSimulatorApp::refreshExtensions() {
    namespace fs = std::filesystem;

    fs::path project_root = m_current_project_path.empty()
                                ? fs::current_path()
                                : fs::path(m_current_project_path).parent_path();
    if (project_root.empty())
        project_root = fs::current_path();

    m_extension_manager.rescan(project_root);
    m_library = ComponentLibrary{};

#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (home) {
        m_library.scan((fs::path(home) / ".rf-sim" / "libraries").string());
    }
    if (fs::exists("rf-sim-libraries")) {
        m_library.scan("rf-sim-libraries");
    }
    // Built-in examples: prefer the exe-relative install location
    // (<exe_dir>/component_data/library) so installed binaries find their
    // shipped data; fall back to the source-tree-relative path (CWD == repo
    // root) when running from a build tree. Same exe-relative convention as
    // layout/ and SessionState.
    const std::filesystem::path exe_builtin_library =
        std::filesystem::path(appExeDir()) / "component_data" / "library";
    if (std::filesystem::exists(exe_builtin_library)) {
        m_library.scan(exe_builtin_library.string());
    } else if (std::filesystem::exists("component_data/library")) {
        m_library.scan("component_data/library");
    }

    for (const auto *pack : m_extension_manager.dataPacks()) {
        for (const auto &root : pack->library_roots)
            m_library.scan(root.string());
    }
}

std::vector<ExtensionMenuEntry>
RfSimulatorApp::externalToolActions(const ExtensionManifest &manifest) const {
    if (!manifest.menus.empty())
        return manifest.menus;
    return {ExtensionMenuEntry{"tools", manifest.name}};
}

void RfSimulatorApp::runExternalTool(const ExtensionManifest &manifest,
                                     std::string_view action_label) {
    namespace fs = std::filesystem;

    const std::string effective_action_label =
        action_label.empty() ? manifest.name : std::string(action_label);
    const fs::path project_root = m_current_project_path.empty()
                                      ? fs::current_path()
                                      : fs::path(m_current_project_path).parent_path();
    const fs::path selected_path =
        m_current_project_path.empty() ? fs::path{} : fs::path(m_current_project_path);
    const fs::path work_dir = fs::temp_directory_path() / "rf-sim-extension-run" / manifest.id;
    const fs::path result_path = work_dir / "result.json";
    std::error_code ec;
    fs::remove(result_path, ec);

    const ExternalToolRequest request{
        "1", effective_action_label, project_root, selected_path, work_dir, result_path};

    const auto result = m_external_tool_runner.run(manifest, request);
    m_extension_result_message = result.ok ? "Extension run succeeded: " + manifest.name
                                           : "Extension run failed: " + result.message;
    if (result.ok)
        refreshExtensions();
}

void RfSimulatorApp::drawExtensionsPanel() {
    if (!m_show_extensions)
        return;

    if (ImGui::Begin("Extensions", &m_show_extensions)) {
        if (ImGui::Button("Refresh"))
            refreshExtensions();
        if (!m_extension_result_message.empty())
            ImGui::TextWrapped("%s", m_extension_result_message.c_str());
        else
            ImGui::TextDisabled("No extension actions run yet.");

        ImGui::Separator();

        for (const auto &record : m_extension_manager.all()) {
            const bool has_manifest = record.manifest.has_value();
            const std::string label =
                has_manifest ? record.manifest->name : record.manifest_path.filename().string();
            const char *status = record.status == ExtensionStatusKind::Ok ? "Ok"
                                 : record.status == ExtensionStatusKind::Incompatible
                                     ? "Incompatible"
                                     : "Invalid";

            ImGui::Text("%s [%s]", label.c_str(), status);
            if (has_manifest && record.status == ExtensionStatusKind::Ok &&
                record.manifest->kind == ExtensionKind::ExternalTool) {
                const auto actions = externalToolActions(*record.manifest);
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    ImGui::SameLine();
                    const std::string button_label = record.manifest->menus.empty()
                                                         ? "Run##" + record.manifest->id
                                                         : actions[i].label + "##" +
                                                               record.manifest->id + "-" +
                                                               std::to_string(i);
                    if (ImGui::Button(button_label.c_str()))
                        runExternalTool(*record.manifest, actions[i].label);
                }
            }

            if (!record.issues.empty()) {
                ImGui::Indent();
                for (const auto &issue : record.issues)
                    ImGui::TextWrapped("%s: %s", issue.field.c_str(), issue.message.c_str());
                ImGui::Unindent();
            }
        }
    }
    ImGui::End();
}

void RfSimulatorApp::saveProject(const std::string &path) {
    m_serializer->save(path);
    m_current_project_path = path;
    m_dirty = false;
}

void RfSimulatorApp::loadProject(const std::string &path) {
    if (!m_serializer->load(path))
        return;
    m_current_project_path = path;
    refreshExtensions();
    m_dirty = false;
}

void RfSimulatorApp::openFileDialog() {
    auto result = pfd::open_file("Open Project", ".",
                                 {"RF Simulator Project (*.rfsim)", "*.rfsim", "All Files", "*"})
                      .result();
    if (!result.empty())
        loadProject(result[0]);
}

void RfSimulatorApp::saveFileDialog() {
    auto result =
        pfd::save_file("Save Project As", ".", {"RF Simulator Project (*.rfsim)", "*.rfsim"})
            .result();
    if (!result.empty())
        saveProject(result);
}

void RfSimulatorApp::openNewComponentForm(const std::string &type) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(type);
    if (!descriptor)
        return;
    m_component_form_is_edit = false;
    m_component_form_destination_root.clear();
    m_component_form_model = std::make_unique<ComponentFormModel>(*descriptor);
    m_component_form_widget = std::make_unique<ComponentFormWidget>(*m_component_form_model);
    m_component_form_error.clear();
    m_show_component_form = true;
}

void RfSimulatorApp::openEditComponentForm(const ComponentDefinition &def) {
    const auto *descriptor = ComponentTypeRegistry::instance().find(def.type);
    if (!descriptor)
        return;
    m_component_form_is_edit = true;
    m_component_form_model = std::make_unique<ComponentFormModel>(*descriptor);
    m_component_form_model->loadFrom(def);
    m_component_form_widget = std::make_unique<ComponentFormWidget>(*m_component_form_model);
    m_component_form_error.clear();
    m_show_component_form = true;
}

bool RfSimulatorApp::saveComponentForm() {
    namespace fs = std::filesystem;
    auto &model = *m_component_form_model;
    auto def = model.buildDefinition();

    std::string root;
    if (m_component_form_is_edit) {
        // Overwrite in place — no rename-on-identity-change.
        def.source_path = model.sourcePath();
    } else {
        root = m_component_form_destination_root;
        if (root.empty()) {
            m_component_form_error = "Choose a destination (Project or Global) before saving.";
            return false;
        }
        std::string safe_man = sanitizePathSegment(def.manufacturer, "unknown");
        std::string safe_pn = sanitizePathSegment(def.part_number, "component");
        fs::path dir = fs::path(root) / def.type / safe_man;
        def.source_path = (dir / (safe_pn + ".json")).string();
        if (fs::exists(def.source_path)) {
            m_component_form_error = "A component already exists at " + def.source_path;
            return false;
        }
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            m_component_form_error = "Could not create directory: " + dir.string();
            return false;
        }
    }

    // Copy S-param file into place next to the destination JSON, if one was picked.
    if (!model.sparamSourcePath().empty()) {
        fs::path dest_dir = fs::path(def.source_path).parent_path();
        fs::path dest_sparam = dest_dir / def.data_files[0].path;
        std::error_code ec;
        fs::copy_file(model.sparamSourcePath(), dest_sparam, fs::copy_options::overwrite_existing,
                      ec);
        if (ec) {
            m_component_form_error = "Failed to copy S-parameter file: " + ec.message();
            return false;
        }
    }

    nlohmann::json j;
    j["schema_version"] = def.schema_version;
    j["type"] = def.type;
    j["part_number"] = def.part_number;
    j["manufacturer"] = def.manufacturer;
    j["description"] = def.description;
    j["parameters"] = def.parameters;
    if (!def.test_conditions.empty())
        j["test_conditions"] = def.test_conditions;
    j["notes"] = def.notes;
    if (!def.data_files.empty()) {
        j["data_files"] = nlohmann::json::array();
        for (const auto &df : def.data_files)
            j["data_files"].push_back({{"type", df.type}, {"path", df.path}});
    }

    std::ofstream out(def.source_path);
    if (!out.is_open()) {
        m_component_form_error = "Could not write " + def.source_path;
        return false;
    }
    out << j.dump(2);
    out.close();

    def.issues = m_library.validate(def.type, def.parameters);
    m_library.upsert(def);
    return true;
}

void RfSimulatorApp::drawComponentFormModal() {
    if (!m_show_component_form)
        return;
    ImGui::OpenPopup("Component Form");
    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Component Form", &m_show_component_form)) {
        ImGui::TextUnformatted(m_component_form_is_edit ? "Edit Component" : "New Component");
        ImGui::Separator();
        if (!m_component_form_is_edit) {
            std::vector<const ComponentTypeDescriptor *> authorable;
            for (auto *d : ComponentTypeRegistry::instance().all())
                if (d->authorable)
                    authorable.push_back(d);
            static int type_idx = 0;
            for (size_t i = 0; i < authorable.size(); ++i)
                if (m_component_form_model->descriptor().type == authorable[i]->type)
                    type_idx = static_cast<int>(i);
            std::vector<const char *> type_names;
            for (auto *d : authorable)
                type_names.push_back(d->type.c_str());
            if (ImGui::Combo("Type", &type_idx, type_names.data(),
                             static_cast<int>(type_names.size())))
                openNewComponentForm(authorable[type_idx]->type);

            const char *roots[] = {"Project (./rf-sim-libraries)", "Global (~/.rf-sim/libraries)"};
            static int root_idx = 0;
            ImGui::Combo("Save To", &root_idx, roots, 2);
            const char *home = std::getenv(
#ifdef _WIN32
                "USERPROFILE"
#else
                "HOME"
#endif
            );
            m_component_form_destination_root =
                root_idx == 0 ? "rf-sim-libraries"
                : home        ? (std::filesystem::path(home) / ".rf-sim" / "libraries").string()
                              : "rf-sim-libraries";
            ImGui::Separator();
        }

        bool save_clicked = m_component_form_widget->draw(m_library);
        if (!m_component_form_error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                               m_component_form_error.c_str());

        if (save_clicked) {
            if (saveComponentForm()) {
                m_show_component_form = false;
                markDirty();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_show_component_form = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void RfSimulatorApp::rewireInputs() {
    for (auto *comp : m_components.all()) {
        int N = comp->numInputPins();
        for (int k = 0; k < N; ++k) {
            int pid = comp->inputPinId(k);
            if (pid >= 0) {
                auto source = m_graph_engine.getSourceForInput(pid);
                IComponentEngine *source_comp = nullptr;
                if (source.node) {
                    for (auto *candidate : m_components.all()) {
                        if (&candidate->node() == source.node) {
                            source_comp = candidate;
                            break;
                        }
                    }
                }
                const int source_pin = source_comp && source.output_index >= 0
                                           ? source_comp->outputPinId(source.output_index)
                                           : -1;
                const bool allowed = graphLinkAllowed(source_comp, comp, source_pin, pid);
                comp->node().inputs[k] =
                    allowed && source.node && source.output_index >= 0 &&
                            static_cast<size_t>(source.output_index) < source.node->outputs.size()
                        ? &source.node->outputs[static_cast<size_t>(source.output_index)]
                        : nullptr;
            } else if (static_cast<size_t>(k) < comp->node().inputs.size()) {
                comp->node().inputs[k] = nullptr;
            }
        }
    }
}

void RfSimulatorApp::update_dsp() {
    rewireInputs();

    std::unordered_map<int, std::function<void()>> updates;
    for (auto *comp : m_components.all()) {
        updates[comp->graphNodeId()] = [comp]() { comp->update(0.0); };
    }

    auto order = m_graph_engine.topologicalOrder();
    for (int node_id : order) {
        auto it = updates.find(node_id);
        if (it != updates.end())
            it->second();
    }

    // Update spectrum view based on probed pins. Each probe resolves to a
    // (node, output-index) pair so OUT2 of a splitter/PFB probes the right
    // Spectrum instead of always outputs[0].
    auto probed_sources = m_graph_engine.probedSignalNodes();
    std::vector<std::string> probe_labels;
    std::vector<std::pair<SignalNode *, int>> probe_targets;
    probe_targets.reserve(probed_sources.size());
    for (const auto &ps : probed_sources) {
        std::string label;
        if (ps.node) {
            for (const auto &node : m_graph_engine.nodes()) {
                if (node.signal_node == ps.node) {
                    label = node.label + " OUT";
                    if (ps.output_index > 0)
                        label += std::to_string(ps.output_index + 1);
                    break;
                }
            }
            probe_targets.emplace_back(ps.node, ps.output_index);
        }
        probe_labels.push_back(label);
    }
    m_spectrum_widget->setProbeLabels(probe_labels);
    m_spectrum_widget->setProbeTargets(probe_targets);

    for (auto *node : m_view_manager.nodes()) {
        if (node) {
            node->view_enabled = std::find_if(probed_sources.begin(), probed_sources.end(),
                                              [node](const SignalSource &ps) {
                                                  return ps.node == node;
                                              }) != probed_sources.end();
        }
    }

    // Sync PFB pointers to spectrum analyzer and inspector panel
    auto pfb_ptrs = m_components.byType<PFBChannelizerEngine>();
    std::vector<PFBChannelizerEngine *> pfb_vec(pfb_ptrs.begin(), pfb_ptrs.end());
    m_spectrum_widget->setPFBs(pfb_vec);
    m_inspector_panel->setPFBs(pfb_vec);
    m_inspector_panel->setPFBWindowVisibility(&m_pfb_views.iqVisibility(),
                                              &m_pfb_views.gridVisibility());
}

void RfSimulatorApp::draw_ui() {
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    // Reset the unsaved dialog flag at the start of each frame.
    // The menu handlers below set it to true only when m_dirty is set.
    m_show_unsaved_dialog = false;

    // File menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                if (m_dirty) {
                    m_pending_action = PendingAction::New;
                    m_show_unsaved_dialog = true;
                } else
                    newProject();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                if (m_dirty) {
                    m_pending_action = PendingAction::Open;
                    m_show_unsaved_dialog = true;
                } else
                    openFileDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (!m_current_project_path.empty())
                    saveProject(m_current_project_path);
                else
                    saveFileDialog();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                saveFileDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                if (m_dirty) {
                    m_pending_action = PendingAction::Exit;
                    m_show_unsaved_dialog = true;
                } else
                    std::exit(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Log", nullptr, &m_show_log);
            ImGui::MenuItem("Spectrum Analyzer", nullptr, &m_show_spectrum);
            ImGui::MenuItem("Network Analyzer", nullptr, &m_show_na);
            ImGui::MenuItem("Properties", nullptr, &m_show_properties);
            ImGui::MenuItem("Node Editor", nullptr, &m_show_node_editor);
            ImGui::MenuItem("Component Library", nullptr, &m_show_library);
            ImGui::Separator();
            if (ImGui::BeginMenu("Layouts")) {
                if (ImGui::MenuItem("Save As...")) {
                    m_layout_name_buf[0] = '\0';
                    m_show_save_layout_dialog = true;
                }
                auto layout_names = m_layout_manager.listNamedLayouts();
                if (ImGui::BeginMenu("Load", !layout_names.empty())) {
                    for (const auto &name : layout_names) {
                        if (ImGui::MenuItem(name.c_str()))
                            m_layout_manager.loadNamedLayout(name);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Manage..."))
                    m_show_manage_layouts_dialog = true;
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("Extensions", nullptr, &m_show_extensions);
            ImGui::Separator();
            for (const auto *tool : m_extension_manager.externalTools()) {
                if (!tool)
                    continue;
                for (const auto &action : externalToolActions(*tool)) {
                    if (action.location == "tools" && ImGui::MenuItem(action.label.c_str()))
                        runExternalTool(*tool, action.label);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("How to Use", "F1"))
                m_show_help = !m_show_help;
            if (ImGui::MenuItem("Tutorial"))
                requestTutorial();
            ImGui::EndMenu();
        }
        // Title / project name on the right
        {
            float tw = ImGui::GetContentRegionAvail().x;
            ImGui::SameLine(tw - 300.0f);
            if (!m_current_project_path.empty()) {
                auto p = m_current_project_path.find_last_of("\\/");
                std::string fname = (p != std::string::npos) ? m_current_project_path.substr(p + 1)
                                                             : m_current_project_path;
                ImGui::Text("%s%s", m_dirty ? "* " : "", fname.c_str());
            } else if (m_dirty) {
                ImGui::Text("*Untitled");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f FPS", io.Framerate);
        }
        ImGui::EndMainMenuBar();
    }

    // Keyboard shortcuts (skip while editing text fields)
    if (!io.WantTextInput) {
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (!m_current_project_path.empty())
                saveProject(m_current_project_path);
            else
                saveFileDialog();
        }
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
            saveFileDialog();
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
            if (m_dirty) {
                m_pending_action = PendingAction::Open;
                m_show_unsaved_dialog = true;
            } else
                openFileDialog();
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N)) {
            if (m_dirty) {
                m_pending_action = PendingAction::New;
                m_show_unsaved_dialog = true;
            } else
                newProject();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F1))
            m_show_help = !m_show_help;
    }

    // First-run tutorial offer. Must stay ahead of the Unsaved Changes block so
    // that "Start Tutorial" can raise that dialog within the same frame.
    if (m_show_tutorial_first_run_prompt) {
        ImGui::OpenPopup("Welcome to Tiny RF Simulator");
    }
    if (ImGui::BeginPopupModal("Welcome to Tiny RF Simulator", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("New here? A short guided tutorial walks you through building");
        ImGui::Text("and probing your first signal chain.");
        ImGui::Spacing();
        ImGui::TextDisabled("Re-run it anytime from Help > Tutorial.");
        ImGui::Separator();
        // Either answer marks the tutorial completed: this prompt is a one-time
        // offer, not a reminder that returns until the walkthrough is finished.
        if (ImGui::Button("Start Tutorial", ImVec2(140, 0))) {
            m_tutorial_state.markCompleted();
            m_show_tutorial_first_run_prompt = false;
            ImGui::CloseCurrentPopup();
            requestTutorial();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not Now", ImVec2(140, 0))) {
            m_tutorial_state.markCompleted();
            m_show_tutorial_first_run_prompt = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Unsaved Changes popup — use a bool flag instead of OpenPopup/BeginPopupModal,
    // which can be unreliable when called from inside a menu bar context.
    if (m_show_unsaved_dialog) {
        ImGui::OpenPopup("Unsaved Changes");
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Save before continuing?");
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (!m_current_project_path.empty())
                saveProject(m_current_project_path);
            else {
                auto path = pfd::save_file("Save Project As", ".",
                                           {"RF Simulator Project (*.rfsim)", "*.rfsim"})
                                .result();
                if (!path.empty())
                    saveProject(path);
            }
            if (!m_dirty) {
                // Save succeeded — execute the pending action now
                auto action = m_pending_action;
                m_pending_action = PendingAction::None;
                m_show_unsaved_dialog = false;
                ImGui::CloseCurrentPopup();
                switch (action) {
                case PendingAction::New:
                    newProject();
                    break;
                case PendingAction::Open:
                    openFileDialog();
                    break;
                case PendingAction::Exit:
                    std::exit(0);
                    break;
                case PendingAction::Tutorial:
                    startTutorial();
                    break;
                default:
                    break;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120, 0))) {
            auto action = m_pending_action;
            m_pending_action = PendingAction::None;
            m_show_unsaved_dialog = false;
            ImGui::CloseCurrentPopup();
            // Execute immediately — user chose to discard
            switch (action) {
            case PendingAction::New:
                newProject();
                break;
            case PendingAction::Open:
                openFileDialog();
                break;
            case PendingAction::Exit:
                std::exit(0);
                break;
            case PendingAction::Tutorial:
                startTutorial();
                break;
            default:
                break;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pending_action = PendingAction::None;
            m_show_unsaved_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Save Layout As popup
    if (m_show_save_layout_dialog) {
        ImGui::OpenPopup("Save Layout As");
    }
    if (ImGui::BeginPopupModal("Save Layout As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_layout_name_buf, sizeof(m_layout_name_buf));
        bool can_save = m_layout_name_buf[0] != '\0';
        if (!can_save)
            ImGui::BeginDisabled();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            m_layout_manager.saveNamedLayout(m_layout_name_buf);
            m_layout_name_buf[0] = '\0';
            m_show_save_layout_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        if (!can_save)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_layout_name_buf[0] = '\0';
            m_show_save_layout_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Manage Layouts popup
    if (m_show_manage_layouts_dialog) {
        ImGui::OpenPopup("Manage Layouts");
    }
    if (ImGui::BeginPopupModal("Manage Layouts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto names = m_layout_manager.listNamedLayouts();
        if (names.empty())
            ImGui::TextDisabled("No saved layouts.");
        for (const auto &name : names) {
            ImGui::PushID(name.c_str());
            if (m_rename_target == name) {
                ImGui::SetNextItemWidth(160);
                ImGui::InputText("##rename", m_rename_buf, sizeof(m_rename_buf));
                ImGui::SameLine();
                if (ImGui::Button("OK")) {
                    if (m_rename_buf[0] != '\0') {
                        if (m_layout_manager.renameNamedLayout(name, m_rename_buf))
                            m_rename_target.clear();
                        else
                            LOG_ERROR("Failed to rename layout '%s' to '%s'", name.c_str(),
                                      m_rename_buf);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("X"))
                    m_rename_target.clear();
            } else {
                ImGui::Text("%s", name.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Load"))
                    m_layout_manager.loadNamedLayout(name);
                ImGui::SameLine();
                if (ImGui::Button("Rename")) {
                    m_rename_target = name;
                    std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", name.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                    m_layout_manager.deleteNamedLayout(name);
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_rename_target.clear();
            m_show_manage_layouts_dialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    drawComponentFormModal();

    if (m_show_node_editor)
        m_graph_widget->draw("Node Editor", &m_show_node_editor);

    if (m_show_spectrum)
        m_spectrum_widget->draw("Spectrum Analyzer", &m_show_spectrum);

    if (m_show_na) {
        m_na_engine.update();
        m_na_widget->draw("Network Analyzer", &m_show_na);
    }

    m_pfb_views.draw();

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        m_generator_widgets[i]->draw("Generators");
    }

    if (m_show_properties && m_inspector_panel)
        m_inspector_panel->draw("Properties", &m_show_properties);

    if (m_show_library && m_library_browser) {
        m_library_browser->draw("Component Library", &m_show_library);
    }
    drawExtensionsPanel();

    if (m_show_log)
        m_log_widget.draw("Log", &m_show_log);

    if (m_show_help)
        m_help_widget.draw("How to Use", &m_show_help);

    if (m_show_tutorial) {
        m_tutorial_widget.draw(m_tutorial_state);
        // Finish/Exit deactivate TutorialState from inside the widget; mirror
        // that back onto the app's visibility flag.
        m_show_tutorial = m_tutorial_state.isActive();
    }
}

RfSimulatorApp::~RfSimulatorApp() {
    m_state.saveBool("WindowState", "Log", m_show_log);
    m_state.saveBool("WindowState", "SpectrumAnalyzer", m_show_spectrum);
    m_state.saveBool("WindowState", "NetworkAnalyzer", m_show_na);
    m_state.saveBool("WindowState", "Properties", m_show_properties);
    m_pfb_views.saveVisibility(m_components, m_state);
    m_state.saveBool("WindowState", "NodeEditor", m_show_node_editor);
    m_state.saveBool("WindowState", "Help", m_show_help);
}
