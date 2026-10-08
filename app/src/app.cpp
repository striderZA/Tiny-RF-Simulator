#include "app.h"
#include "attenuator_engine.h"
#include "coax_cable_engine.h"
#include "combiner_engine.h"
#include "imgui.h"
#include "imnodes.h"
#include "logging_core.h"
#include "logging_widget.h"
#include "output_snr.h"
#include "pfb_channelizer_engine.h"
#include <algorithm>
#include <chrono>
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

RfSimulatorApp::RfSimulatorApp() : m_graph_editor_actions(m_circuit_runtime) {
    m_editor_commands.onComponentsChanged = [this]() { syncComponentViews(); };

    // The widget's edit requests are forwarded verbatim: EditorCommands owns
    // each edit's side effects (group boundaries, view sync, revision), so the
    // editor and the test commands below cannot diverge.
    NodeGraphWidgetActions editor_actions;
    editor_actions.connectLink = [this](int start_pin, int end_pin) {
        return m_editor_commands.connect(start_pin, end_pin);
    };
    editor_actions.disconnectLink = [this](int link_id) {
        return m_editor_commands.disconnect(link_id);
    };
    editor_actions.removeComponent = [this](int id) {
        return m_editor_commands.removeComponent(id);
    };
    editor_actions.duplicateComponent = [this](int id) { return duplicateComponent(id); };
    editor_actions.addProbePin = [this](int pin_id) {
        return m_editor_commands.addProbePin(pin_id);
    };
    editor_actions.removeProbePin = [this](int pin_id) {
        return m_editor_commands.removeProbePin(pin_id);
    };
    editor_actions.createGroup = [this](std::string name, std::vector<int> members) {
        return m_editor_commands.createGroup(std::move(name), std::move(members));
    };
    editor_actions.removeGroup = [this](int group_id) {
        return m_editor_commands.removeGroup(group_id);
    };
    editor_actions.renameGroup = [this](int group_id, std::string name) {
        return m_editor_commands.renameGroup(group_id, std::move(name));
    };
    editor_actions.setGroupCollapsed = [this](int group_id, bool collapsed) {
        return m_editor_commands.setGroupCollapsed(group_id, collapsed);
    };
    editor_actions.selectGroup = [this](int group_id) { m_editor_commands.selectGroup(group_id); };
    m_graph_widget =
        std::make_unique<NodeGraphWidget>(m_circuit_runtime.graph(), std::move(editor_actions));
    m_serializer = std::make_unique<ProjectSerializer>(
        m_circuit_runtime, m_graph_editor_actions, *m_graph_widget, m_pfb_views,
        m_receiver_requirements, m_show_log, m_show_spectrum, m_show_properties, m_show_node_editor,
        m_na_engine);

    std::vector<NodeGraphWidget::AddableComponent> addable;
    for (const auto *desc : ComponentTypeRegistry::instance().all()) {
        addable.push_back(
            {desc->menu_label, [this, desc](ImVec2 pos) { addComponent(desc, pos); }});
        m_graph_widget->registerNodeKind(desc->label_prefix, desc->kind);
    }
    m_graph_widget->setAddableComponents(std::move(addable));
    m_graph_widget->onNodeMoved = [this]() { markDirty(); };
    m_graph_widget->onNodeHover = [this](int id) {
        NodeHoverInfo info;
        info.summary = m_circuit_runtime.components().hoverSummary(id);
        if (IComponentEngine *component = m_circuit_runtime.components().find(id))
            info.snr_dB = computeOutputSnr(*component, 0, m_spectrum_engine).snr_dB;
        return info;
    };

    if (const auto *descriptor = ComponentTypeRegistry::instance().find("generator")) {
        if (auto *engine = m_circuit_runtime.createComponent(descriptor->create))
            static_cast<SignalGeneratorEngine *>(engine)->addTone(100e6, -20.0);
    }
    if (const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier"))
        m_circuit_runtime.createComponent(descriptor->create);

    m_inspector_panel = std::make_unique<InspectorPanel>(
        m_circuit_runtime.graph(), m_circuit_runtime.components(), m_editor_commands);
    m_inspector_panel->registerDrawers(ComponentTypeRegistry::instance());
    m_inspector_panel->onRemoveNode = [this](int graph_node_id) {
        (void)m_editor_commands.removeComponent(graph_node_id);
    };

    m_inspector_panel->setViewToggles({&m_show_log, &m_show_spectrum, &m_show_properties,
                                       nullptr, // iq_plot (per-PFB toggles used instead)
                                       &m_show_node_editor});
    m_inspector_panel->onParamChange = [this]() { markDirty(); };

    refreshExtensions();

    m_library_browser = std::make_unique<LibraryBrowserWidget>(m_library);
    m_library_browser->onInsert = [this](const ComponentDefinition &def) {
        (void)m_editor_commands.addLibraryPart(m_library, def);
    };

    m_library_browser->onNewComponent = [this]() { openNewComponentForm("amplifier"); };
    m_library_browser->onEditComponent = [this](const ComponentDefinition &def) {
        openEditComponentForm(def);
    };

    m_spectrum_widget = std::make_unique<SpectrumAnalyzerWidget>(m_spectrum_engine,
                                                                 m_circuit_runtime.viewManager());
    m_na_widget = std::make_unique<NetworkAnalyzerWidget>(m_na_engine, m_circuit_runtime.graph());
    m_receiver_requirements_widget = std::make_unique<ReceiverRequirementsWidget>();
    m_receiver_requirements_widget->onChange = [this]() { markDirty(); };
    m_power_meter_widget =
        std::make_unique<PowerMeterWidget>(m_power_meter_engine, m_circuit_runtime.graph());
    // Sweep-param/Point A/B edits in the Network Analyzer panel are project
    // state (persisted by ProjectSerializer) — mark the project dirty exactly
    // like InspectorPanel::onParamChange does for component params.
    m_na_widget->onParamChange = [this]() { markDirty(); };
    m_calculator_widget = std::make_unique<PfbCalculatorWidget>(m_circuit_runtime.components());
    m_calculator_widget->onParamChange = [this]() { markDirty(); };
    m_test_flow_widget =
        std::make_unique<TestFlowWidget>(m_circuit_runtime.components(), m_circuit_runtime.graph());

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

// --- Measurement-chain host adapter ----------------------------------------
// The app resolves live engines through ComponentRegistry and creates each
// private clone pass with its own graph/registry and type-registry factory.

RfSimulatorApp::NaHost::NaHost(const ComponentRegistry &components) : m_components(components) {}

IComponentEngine *RfSimulatorApp::NaHost::componentForNode(int graph_node_id) const {
    return m_components.find(graph_node_id);
}

std::unique_ptr<IMeasurementChainScratch> RfSimulatorApp::NaHost::beginScratchPass() const {
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
    IComponentEngine *comp = m_editor_commands.createComponent(desc->create);
    if (!comp)
        return;
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImNodes::SetNodeEditorSpacePos(comp->graphNodeId(), pos);
}

void RfSimulatorApp::syncComponentViews() {
    m_pfb_views.sync(m_circuit_runtime.components(), m_state);
}

void RfSimulatorApp::load_window_states() {
    m_show_log = m_state.loadBool("WindowState", "Log", true);
    m_show_spectrum = m_state.loadBool("WindowState", "SpectrumAnalyzer", true);
    m_show_na = m_state.loadBool("WindowState", "NetworkAnalyzer", false);
    m_show_power_meter = m_state.loadBool("WindowState", "PowerMeter", false);
    m_show_properties = m_state.loadBool("WindowState", "Properties", true);
    m_show_node_editor = m_state.loadBool("WindowState", "NodeEditor", true);
    m_show_help = m_state.loadBool("WindowState", "Help", false);
    m_show_receiver_requirements = m_state.loadBool("WindowState", "ReceiverRequirements", false);
    m_show_calculator = m_state.loadBool("WindowState", "FilterCalculator", false);
    m_show_test_flow = m_state.loadBool("WindowState", "TestFlow", false);
}

bool RfSimulatorApp::duplicateComponent(int graph_node_id) {
    IComponentEngine *src = m_circuit_runtime.components().find(graph_node_id);
    if (!src)
        return false;

    // Capture source position before creating the new node
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImVec2 src_pos = ImNodes::GetNodeEditorSpacePos(graph_node_id);
    constexpr float OFFSET = 40.0f;

    // Capture source part number for copying to the duplicate
    std::string src_part_number;
    for (const auto &gn : m_circuit_runtime.graph().nodes()) {
        if (gn.node_id == graph_node_id) {
            src_part_number = gn.part_number;
            break;
        }
    }

    // Clone via the registry: create a default engine, then copy params through
    // serialize/deserialize inside the factory, so component-bound views are
    // created for the fully configured copy.
    const auto *desc = ComponentTypeRegistry::instance().find(src->type_name());
    if (!desc)
        return false;
    const nlohmann::json params = src->serialize();
    IComponentEngine *copy = m_editor_commands.createComponent(
        [desc, &params](ComponentRegistry &components, NodeGraphEngine &graph, int id) {
            IComponentEngine *engine = desc->create(components, graph, id);
            if (engine)
                engine->deserialize(params);
            return engine;
        });
    if (!copy)
        return false;
    int new_nid = copy->graphNodeId();
    // Register with imnodes pool and set position
    ImNodes::EditorContextSet(m_graph_widget->context());
    ImNodes::SetNodeEditorSpacePos(new_nid, ImVec2(src_pos.x + OFFSET, src_pos.y + OFFSET));
    // Copy library part number
    if (!src_part_number.empty())
        m_graph_editor_actions.setNodePartNumber(new_nid, src_part_number);
    return true;
}
void RfSimulatorApp::newProject() {
    m_power_meter_widget->clearSource();
    m_serializer->reset();
    syncComponentViews();
    m_spectrum_widget->setProbeLabels({});
    m_current_project_path.clear();
    refreshExtensions();
    m_editor_commands.markClean();
    // The reset replaced every engine, so the panel's snapshots and result refer
    // to a circuit that no longer exists. The retained flow selection is
    // revalidated against the new circuit by the panel itself.
    m_test_flow_widget->resetAfterCircuitReload();
}

void RfSimulatorApp::requestTutorial() {
    // Same guard New/Open/Exit use — startTutorial() discards the current
    // project, so the user must get the chance to save first.
    if (isDirty()) {
        m_pending_action = PendingAction::Tutorial;
        m_show_unsaved_dialog = true;
    } else
        startTutorial();
}

void RfSimulatorApp::startTutorial() {
    // Reset to the same Generator + Amplifier pair the app seeds on first launch,
    // so every step's instruction matches what the user is actually looking at.
    newProject();
    if (const auto *descriptor = ComponentTypeRegistry::instance().find("generator")) {
        if (auto *engine = m_circuit_runtime.createComponent(descriptor->create))
            static_cast<SignalGeneratorEngine *>(engine)->addTone(100e6, -20.0);
    }
    if (const auto *descriptor = ComponentTypeRegistry::instance().find("amplifier"))
        m_circuit_runtime.createComponent(descriptor->create);
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

// The test commands below forward to the same EditorCommands the editor uses,
// so fixtures built through them carry the editor's side effects (group
// boundaries, component-bound views, project revision).
IComponentEngine *RfSimulatorApp::testCreateComponent(std::string_view type, int engine_id) {
    const ComponentTypeDescriptor *descriptor = ComponentTypeRegistry::instance().find(type);
    return descriptor ? testCreateComponent(descriptor->create, engine_id) : nullptr;
}

IComponentEngine *RfSimulatorApp::testCreateComponent(
    const std::function<IComponentEngine *(ComponentRegistry &, NodeGraphEngine &, int)> &factory,
    int engine_id) {
    if (!factory)
        return nullptr;
    // The fixture picks the engine id, so the runtime's counter is restored and
    // a later default-id component cannot collide with the test-chosen one.
    const int next_component_id = m_circuit_runtime.nextComponentId();
    const auto test_factory = [&factory, engine_id](ComponentRegistry &components,
                                                    NodeGraphEngine &graph, int) {
        return factory(components, graph, engine_id);
    };
    try {
        IComponentEngine *engine = m_editor_commands.createComponent(test_factory);
        m_circuit_runtime.setNextComponentId(next_component_id);
        return engine;
    } catch (...) {
        m_circuit_runtime.setNextComponentId(next_component_id);
        throw;
    }
}

std::optional<int> RfSimulatorApp::testConnectLink(int start_pin_id, int end_pin_id) {
    return m_editor_commands.connect(start_pin_id, end_pin_id);
}

bool RfSimulatorApp::testDisconnectLink(int link_id) {
    return m_editor_commands.disconnect(link_id);
}

bool RfSimulatorApp::testRemoveComponent(int graph_node_id) {
    return m_editor_commands.removeComponent(graph_node_id);
}

bool RfSimulatorApp::testAddProbePin(int pin_id) { return m_editor_commands.addProbePin(pin_id); }

bool RfSimulatorApp::testRemoveProbePin(int pin_id) {
    return m_editor_commands.removeProbePin(pin_id);
}

int RfSimulatorApp::testCreateGroup(std::string name, std::vector<int> member_node_ids) {
    return m_editor_commands.createGroup(std::move(name), std::move(member_node_ids));
}

bool RfSimulatorApp::testRemoveGroup(int group_id) {
    return m_editor_commands.removeGroup(group_id);
}

bool RfSimulatorApp::testSetGroupCollapsed(int group_id, bool collapsed) {
    return m_editor_commands.setGroupCollapsed(group_id, collapsed);
}

void RfSimulatorApp::markDirty() { m_editor_commands.markModified(); }
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

    // Fail closed for any caller: a tool that ships with the open project must
    // be explicitly trusted before its entry point is launched (issue #45).
    if (extensionRequiresTrust(manifest)) {
        m_extension_result_message = "Extension run failed: " + manifest.name +
                                     " is not trusted (it ships with the open project)";
        requestExtensionTrust(manifest);
        return;
    }

    const std::string effective_action_label =
        action_label.empty() ? manifest.name : std::string(action_label);
    const fs::path project_root = m_current_project_path.empty()
                                      ? fs::current_path()
                                      : fs::path(m_current_project_path).parent_path();
    const fs::path selected_path =
        m_current_project_path.empty() ? fs::path{} : fs::path(m_current_project_path);

    // Extension workspaces live under a canonical temp root. Manifest ids are
    // allowlist-validated at parse time, but a hand-built manifest could still
    // smuggle traversal into the workspace path, so refuse unless the
    // canonical workspace stays inside the run root (issue #80).
    const fs::path workspace_root = fs::temp_directory_path() / "rf-sim-extension-run";
    std::error_code ec;
    fs::create_directories(workspace_root, ec);
    if (ec || !canonicalPathWithinRoot(workspace_root, workspace_root / manifest.id)) {
        m_extension_result_message =
            "Extension run failed: extension id is not allowed for execution";
        return;
    }

    // The runner derives a fresh per-invocation workspace below this root, so
    // no stale-result cleanup is needed here (issue #80).
    const ExternalToolRequest request{"1", effective_action_label, project_root, selected_path,
                                      workspace_root / manifest.id};

    const auto result = m_external_tool_runner.run(manifest, request);
    m_extension_result_message = result.ok ? "Extension run succeeded: " + manifest.name
                                           : "Extension run failed: " + result.message;
    if (result.ok)
        refreshExtensions();
}

// Exhaustive so a new ExtensionStatusKind cannot silently report "Invalid" in
// the Extensions panel.
static const char *extensionStatusName(ExtensionStatusKind status) {
    switch (status) {
    case ExtensionStatusKind::Ok:
        return "Ok";
    case ExtensionStatusKind::Incompatible:
        return "Incompatible";
    case ExtensionStatusKind::Shadowed:
        return "Shadowed";
    case ExtensionStatusKind::Invalid:
        break;
    }
    return "Invalid";
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

            ImGui::Text("%s [%s]", label.c_str(), extensionStatusName(record.status));
            if (has_manifest && record.status == ExtensionStatusKind::Ok &&
                record.manifest->kind == ExtensionKind::ExternalTool) {
                const bool needs_trust = extensionRequiresTrust(*record.manifest);
                if (m_extension_manager.isProjectLocal(*record.manifest))
                    drawExternalToolTrustControls(*record.manifest, needs_trust);
                if (!needs_trust) {
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
            }

            if (!record.shadow_detail.empty()) {
                ImGui::Indent();
                ImGui::TextWrapped("Ignored: %s", record.shadow_detail.c_str());
                ImGui::Unindent();
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

bool RfSimulatorApp::extensionRequiresTrust(const ExtensionManifest &manifest) const {
    return manifest.kind == ExtensionKind::ExternalTool &&
           m_extension_manager.isProjectLocal(manifest) &&
           !m_extension_trust.isApproved(manifest.root_dir);
}

std::vector<const ExtensionManifest *> RfSimulatorApp::toolsMenuEntries() const {
    std::vector<const ExtensionManifest *> entries;
    for (const auto *tool : m_extension_manager.externalTools()) {
        if (tool && !extensionRequiresTrust(*tool))
            entries.push_back(tool);
    }
    return entries;
}

void RfSimulatorApp::requestExtensionTrust(const ExtensionManifest &manifest) {
    m_pending_trust_manifest = manifest;
    m_show_extension_trust_prompt = true;
}

void RfSimulatorApp::grantPendingExtensionTrust() {
    if (m_pending_trust_manifest && !m_extension_trust.approve(*m_pending_trust_manifest))
        m_extension_result_message =
            "Extension trust could not be saved: " + m_extension_trust.storePath().string();
    m_pending_trust_manifest.reset();
    m_show_extension_trust_prompt = false;
}

void RfSimulatorApp::denyPendingExtensionTrust() {
    m_pending_trust_manifest.reset();
    m_show_extension_trust_prompt = false;
}

// The manifest supplies name and entry point, so the panel spells out the
// project-local origin and the exact file that would run before offering the
// only two actions: trust the folder, or revoke a previous decision.
void RfSimulatorApp::drawExternalToolTrustControls(const ExtensionManifest &manifest,
                                                   bool needs_trust) {
    ImGui::Indent();
    if (needs_trust)
        ImGui::TextWrapped("Untrusted (ships with the open project). Trust it to run it.");
    else
        ImGui::TextWrapped("Trusted (ships with the open project)");
    ImGui::TextWrapped("Entry point: %s", manifest.entry_path.string().c_str());
    ImGui::TextWrapped("Extension root: %s", manifest.root_dir.string().c_str());

    const auto entry = m_extension_trust.entryFor(manifest.root_dir);
    if (entry && (entry->id != manifest.id || entry->version != manifest.version ||
                  entry->entry_path != manifest.entry_path.generic_string()))
        ImGui::TextWrapped(
            "Trusted for a different manifest (id '%s', version %s, entry %s); revoke to review.",
            entry->id.c_str(), entry->version.c_str(), entry->entry_path.c_str());

    if (needs_trust) {
        if (ImGui::Button(("Trust...##" + manifest.id).c_str()))
            requestExtensionTrust(manifest);
    } else if (ImGui::Button(("Revoke trust##" + manifest.id).c_str())) {
        if (!m_extension_trust.revoke(manifest.root_dir))
            m_extension_result_message =
                "Extension trust could not be saved: " + m_extension_trust.storePath().string();
    }
    ImGui::Unindent();
}

void RfSimulatorApp::drawExtensionTrustPrompt() {
    if (m_show_extension_trust_prompt) {
        ImGui::OpenPopup("Untrusted Extension");
    }
    if (!ImGui::BeginPopupModal("Untrusted Extension", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (m_pending_trust_manifest) {
        const ExtensionManifest &manifest = *m_pending_trust_manifest;
        ImGui::TextWrapped("Run code from the project you have open?");
        ImGui::Separator();
        ImGui::Text("Name: %s", manifest.name.c_str());
        ImGui::Text("Id: %s", manifest.id.c_str());
        ImGui::Text("Version: %s", manifest.version.c_str());
        if (!manifest.author.empty())
            ImGui::Text("Author: %s", manifest.author.c_str());
        ImGui::TextWrapped("Entry point: %s", manifest.entry_path.string().c_str());
        ImGui::TextWrapped("Extension root: %s", manifest.root_dir.string().c_str());
        ImGui::Separator();
        ImGui::TextWrapped("This code ships with the open project and runs with your account's "
                           "rights. Trust it only if you have read it.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Trust", ImVec2(140, 0))) {
        grantPendingExtensionTrust();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(140, 0))) {
        denyPendingExtensionTrust();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RfSimulatorApp::saveProject(const std::string &path) {
    // Only a successful write may clear dirty state or adopt the path: a
    // failed save must leave the project dirty on its previous path so the
    // user keeps the chance to retry (issue #77).
    if (!m_serializer->save(path))
        return;
    m_current_project_path = path;
    m_editor_commands.markClean();
}

void RfSimulatorApp::loadProject(const std::string &path) {
    const bool loaded = m_serializer->load(path);
    // Every outcome re-syncs component-bound views: a successful or resetting
    // load replaced the components, and an intact failure leaves them as-is.
    syncComponentViews();
    if (!loaded) {
        if (m_serializer->lastLoadReset()) {
            m_power_meter_widget->clearSource();
            // The failed load already destroyed the live project, so the
            // previous file must never remain the next Ctrl+S save target —
            // after a failure the empty project is unsaved, not "old.rfsim"
            // (issue #113). A failure that left state intact keeps the path.
            m_current_project_path.clear();
            m_editor_commands.markModified();
            // Extension discovery is rooted at the project directory, so it
            // must be re-derived once the path is gone: otherwise the
            // destroyed project's project-local tools and data packs stay
            // active under the now-untitled project. Clear-then-refresh is the
            // same order as newProject(), so the rescan re-roots at the CWD.
            refreshExtensions();
            // The failed load already destroyed the live circuit, so this is a
            // reload like any other: the panel's latch, its notice, and its
            // stale result all describe a circuit that no longer exists. A
            // failure that leaves the circuit intact (handled below) is not a
            // reload and must not touch them. The retained flow selection is
            // revalidated against the emptied circuit by the panel itself.
            m_test_flow_widget->resetAfterCircuitReload();
        }
        return;
    }
    m_power_meter_widget->clearSource();
    m_current_project_path = path;
    refreshExtensions();
    m_editor_commands.markClean();
    // A load that replaced the circuit — this successful one, or a failed one
    // that reset it (handled above) — is a circuit reload for the panel, so its
    // latch and stale result are cleared; a failed load that left the live
    // circuit intact must not discard them. The retained flow selection is
    // validated against the new circuit by the panel itself.
    m_test_flow_widget->resetAfterCircuitReload();
}

void RfSimulatorApp::openTestFlowDialog() {
    auto result = pfd::open_file("Open Test Flow", ".", {"JSON flow (*.json)", "*.json"}).result();
    if (!result.empty())
        m_test_flow_widget->loadFlow(result[0]);
}

void RfSimulatorApp::exportTestFlowDialog() {
    auto path = pfd::save_file("Export Test Flow Results", ".", {"JSON results (*.json)", "*.json"})
                    .result();
    if (!path.empty())
        m_test_flow_widget->exportResult(path);
}

void RfSimulatorApp::saveTestFlowDialog() {
    auto path = pfd::save_file("Save Test Flow", ".", {"JSON flow (*.json)", "*.json"}).result();
    if (!path.empty())
        m_test_flow_widget->saveFlow(path);
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

// Test-only entry points for the authoring form (issue #120); see app.h. They
// open the real form and let a test choose a destination root or a staged
// S-param path that the UI would otherwise supply through a button / file
// picker, so the save path under test is the production one.
void RfSimulatorApp::testOpenNewComponentForm(const std::string &type,
                                              const std::string &destination_root) {
    openNewComponentForm(type);
    m_component_form_destination_root = destination_root;
}

void RfSimulatorApp::testOpenEditComponentForm(const ComponentDefinition &def) {
    openEditComponentForm(def);
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

    // Preflight the S-parameter destination before staging it beside the component JSON.
    const bool has_sparam_pick = !model.sparamSourcePath().empty();
    fs::path dest_sparam;
    bool destination_exists = false;
    if (has_sparam_pick) {
        // Issue #120: never join an unchecked data-file name onto dest_dir.
        // ComponentFormModel::buildDefinition() already derives the name from a
        // sanitized part number, so through the authoring form this gate cannot
        // currently fire — it is deliberate defence in depth for a definition
        // that reaches this point by any other route. The gate's rejection table
        // is unit-tested against dataFileCopyDestination(), and the save path
        // around it (both the new-entry and the edit coordinate) end to end in
        // tests/test_path_containment.cpp.
        const std::string data_name =
            def.data_files.empty() ? std::string() : def.data_files.front().path;
        fs::path dest_dir = fs::path(def.source_path).parent_path();
        const auto copy_destination = dataFileCopyDestination(dest_dir.string(), data_name);
        if (!copy_destination) {
            m_component_form_error = "Refusing unsafe S-parameter file name '" + data_name +
                                     "': expected a plain file name inside the library root.";
            return false;
        }
        dest_sparam = *copy_destination;
        std::error_code ec;
        const auto destination_status = fs::symlink_status(dest_sparam, ec);
        if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
        } else if (ec) {
            m_component_form_error = "Could not inspect S-parameter destination: " + ec.message();
            return false;
        }
        if (!ec && fs::is_symlink(destination_status)) {
            m_component_form_error = "Refusing to overwrite symbolic link '" + data_name + "'.";
            return false;
        }

        destination_exists = !ec && fs::exists(destination_status);
        if (destination_exists) {
            const auto path_references_destination = [&](const std::string &path,
                                                         const std::string &source_path) {
                if (source_path.empty())
                    return false;
                fs::path referenced_path(path);
                for (const auto &part : referenced_path)
                    if (part == "..")
                        return false;
                const fs::path definition_directory = fs::path(source_path).parent_path();
                if (definition_directory.empty())
                    return false;
                if (!referenced_path.is_absolute())
                    referenced_path = definition_directory / referenced_path;

                std::error_code containment_error;
                const fs::path canonical_root =
                    fs::weakly_canonical(definition_directory, containment_error);
                if (containment_error)
                    return false;
                containment_error.clear();
                const fs::path canonical_reference =
                    fs::weakly_canonical(referenced_path, containment_error);
                if (containment_error)
                    return false;
                auto root_part = canonical_root.begin();
                auto reference_part = canonical_reference.begin();
                for (; root_part != canonical_root.end(); ++root_part, ++reference_part)
                    if (reference_part == canonical_reference.end() ||
                        *root_part != *reference_part)
                        return false;

                std::error_code compare_error;
                return fs::equivalent(canonical_reference, dest_sparam, compare_error) &&
                       !compare_error;
            };
            bool belongs_to_edited_component = false;
            if (m_component_form_is_edit) {
                for (const auto &file : model.originalDataFiles()) {
                    if (file.type == "s_parameters" &&
                        path_references_destination(file.path, model.sourcePath())) {
                        belongs_to_edited_component = true;
                        break;
                    }
                }
                for (const char *key : {"sparam_filepath", "sparam_path"}) {
                    const auto path = model.parameter(key);
                    if (path.is_string() &&
                        path_references_destination(path.get<std::string>(), model.sourcePath())) {
                        belongs_to_edited_component = true;
                        break;
                    }
                }
            }

            bool referenced_by_another_component = false;
            if (belongs_to_edited_component) {
                for (const auto *other : m_library.all()) {
                    if (!other || other->source_path == model.sourcePath())
                        continue;
                    for (const auto &file : other->data_files) {
                        if (file.type == "s_parameters" &&
                            path_references_destination(file.path, other->source_path)) {
                            referenced_by_another_component = true;
                            break;
                        }
                    }
                    if (referenced_by_another_component || !other->parameters.is_object())
                        continue;
                    for (const char *key : {"sparam_filepath", "sparam_path"}) {
                        if (!other->parameters.contains(key))
                            continue;
                        const auto &path = other->parameters[key];
                        if (path.is_string() && path_references_destination(path.get<std::string>(),
                                                                            other->source_path)) {
                            referenced_by_another_component = true;
                            break;
                        }
                    }
                    if (referenced_by_another_component)
                        break;
                }
            }

            if (!belongs_to_edited_component || referenced_by_another_component) {
                m_component_form_error = "Refusing to overwrite existing S-parameter file '" +
                                         data_name +
                                         "'; it may be referenced by another component.";
                return false;
            }
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

    static unsigned long long save_sequence = 0;
    const std::string save_id =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(++save_sequence);
    fs::path definition_temp = def.source_path;
    definition_temp += ".component-tmp-" + save_id;
    fs::path sparam_temp;
    fs::path sparam_backup;
    if (has_sparam_pick) {
        sparam_temp = dest_sparam;
        sparam_temp += ".sparam-tmp-" + save_id;
        sparam_backup = dest_sparam;
        sparam_backup += ".sparam-bak-" + save_id;
    }

    const auto temp_path_available = [](const fs::path &path) {
        std::error_code check_ec;
        const auto status = fs::symlink_status(path, check_ec);
        if (check_ec == std::errc::no_such_file_or_directory)
            return true;
        return !check_ec && !fs::exists(status) && !fs::is_symlink(status);
    };
    if (!temp_path_available(definition_temp) ||
        (has_sparam_pick &&
         (!temp_path_available(sparam_temp) || !temp_path_available(sparam_backup)))) {
        m_component_form_error = "Could not reserve temporary component-save paths.";
        return false;
    }

    const auto remove_temp = [](const fs::path &path) {
        if (path.empty())
            return;
        std::error_code ignored;
        fs::remove(path, ignored);
    };
    std::ofstream out(definition_temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        m_component_form_error = "Could not open temporary component file for writing.";
        return false;
    }
    out << j.dump(2);
    out.flush();
    if (!out) {
        out.close();
        remove_temp(definition_temp);
        m_component_form_error = "Could not write temporary component file.";
        return false;
    }
    out.close();
    if (!out) {
        remove_temp(definition_temp);
        m_component_form_error = "Could not close temporary component file.";
        return false;
    }

    std::error_code ec;
    if (has_sparam_pick) {
        fs::copy_file(model.sparamSourcePath(), sparam_temp, fs::copy_options::none, ec);
        if (ec) {
            remove_temp(definition_temp);
            remove_temp(sparam_temp);
            m_component_form_error = "Failed to stage S-parameter file: " + ec.message();
            return false;
        }

        ec.clear();
        const auto current_status = fs::symlink_status(dest_sparam, ec);
        if (ec == std::errc::no_such_file_or_directory) {
            ec.clear();
        } else if (ec) {
            remove_temp(definition_temp);
            remove_temp(sparam_temp);
            m_component_form_error = "Could not recheck S-parameter destination: " + ec.message();
            return false;
        }
        if (!ec && fs::is_symlink(current_status)) {
            remove_temp(definition_temp);
            remove_temp(sparam_temp);
            m_component_form_error =
                "Refusing to overwrite symbolic link '" + dest_sparam.filename().string() + "'.";
            return false;
        }
        const bool destination_still_exists = !ec && fs::exists(current_status);
        if (destination_still_exists != destination_exists) {
            remove_temp(definition_temp);
            remove_temp(sparam_temp);
            m_component_form_error = "S-parameter destination changed while saving.";
            return false;
        }

        if (destination_exists) {
            ec.clear();
            fs::rename(dest_sparam, sparam_backup, ec);
            if (ec) {
                remove_temp(definition_temp);
                remove_temp(sparam_temp);
                m_component_form_error =
                    "Could not preserve existing S-parameter file: " + ec.message();
                return false;
            }
        }
        ec.clear();
        fs::rename(sparam_temp, dest_sparam, ec);
        if (ec) {
            std::error_code rollback_ec;
            if (destination_exists)
                fs::rename(sparam_backup, dest_sparam, rollback_ec);
            remove_temp(definition_temp);
            remove_temp(sparam_temp);
            m_component_form_error = "Could not install S-parameter file: " + ec.message();
            if (rollback_ec)
                m_component_form_error +=
                    "; restoring the original also failed: " + rollback_ec.message();
            return false;
        }
    }

    ec.clear();
    fs::rename(definition_temp, def.source_path, ec);
    if (ec) {
        std::error_code rollback_ec;
        if (has_sparam_pick) {
            if (destination_exists)
                fs::rename(sparam_backup, dest_sparam, rollback_ec);
            else
                fs::remove(dest_sparam, rollback_ec);
        }
        remove_temp(definition_temp);
        m_component_form_error = "Could not replace component file: " + ec.message();
        if (rollback_ec)
            m_component_form_error +=
                "; restoring the previous S-parameter asset also failed: " + rollback_ec.message();
        return false;
    }

    if (has_sparam_pick && destination_exists) {
        ec.clear();
        fs::remove(sparam_backup, ec);
        if (ec)
            LOG_WARN("Could not remove S-parameter backup %s: %s", sparam_backup.string().c_str(),
                     ec.message().c_str());
    }

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

void RfSimulatorApp::update_dsp() {
    m_circuit_runtime.update(0.0);

    const auto &graph = m_circuit_runtime.graph();
    const auto &view_manager = m_circuit_runtime.viewManager();
    // Update spectrum view based on probed pins. Each probe resolves to a
    // (node, output-index) pair so OUT2 of a splitter/PFB probes the right
    // Spectrum instead of always outputs[0].
    const auto probed_sources = graph.probedSignalNodes();
    std::vector<std::string> probe_labels;
    std::vector<std::pair<SignalNode *, int>> probe_targets;
    probe_targets.reserve(probed_sources.size());
    for (const auto &ps : probed_sources) {
        std::string label;
        if (ps.node) {
            for (const auto &node : graph.nodes()) {
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

    for (auto *node : view_manager.nodes()) {
        if (node) {
            node->view_enabled = std::find_if(probed_sources.begin(), probed_sources.end(),
                                              [node](const SignalSource &ps) {
                                                  return ps.node == node;
                                              }) != probed_sources.end();
        }
    }

    // Sync PFB pointers to spectrum analyzer and inspector panel
    auto pfb_ptrs = m_circuit_runtime.components().byType<PFBChannelizerEngine>();
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
    // The menu handlers below set it to true only when the project is dirty.
    m_show_unsaved_dialog = false;

    // File menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                if (isDirty()) {
                    m_pending_action = PendingAction::New;
                    m_show_unsaved_dialog = true;
                } else
                    newProject();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                if (isDirty()) {
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
                if (isDirty()) {
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
            ImGui::MenuItem("Receiver Requirements", nullptr, &m_show_receiver_requirements);
            ImGui::MenuItem("Power Meter", nullptr, &m_show_power_meter);
            ImGui::MenuItem("Properties", nullptr, &m_show_properties);
            ImGui::MenuItem("Node Editor", nullptr, &m_show_node_editor);
            ImGui::MenuItem("Component Library", nullptr, &m_show_library);
            ImGui::MenuItem("Filter Calculator", nullptr, &m_show_calculator);
            ImGui::MenuItem("Test Flow", nullptr, &m_show_test_flow);
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
            for (const auto *tool : toolsMenuEntries()) {
                const auto actions = externalToolActions(*tool);
                for (std::size_t i = 0; i < actions.size(); ++i) {
                    if (actions[i].location != "tools")
                        continue;
                    // The label doubles as the action's dispatch key, so it is
                    // handed to runExternalTool() untouched; only the ImGui ID
                    // carries the disambiguator. Extension ids are deduplicated
                    // but labels are not, and a shared ID lets a click on one
                    // tool's row launch another's.
                    const std::string item_label =
                        actions[i].label + "##" + tool->id + "-" + std::to_string(i);
                    if (ImGui::MenuItem(item_label.c_str()))
                        runExternalTool(*tool, actions[i].label);
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
                ImGui::Text("%s%s", isDirty() ? "* " : "", fname.c_str());
            } else if (isDirty()) {
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
            if (isDirty()) {
                m_pending_action = PendingAction::Open;
                m_show_unsaved_dialog = true;
            } else
                openFileDialog();
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N)) {
            if (isDirty()) {
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
            if (!isDirty()) {
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

    if (m_show_na || m_show_receiver_requirements)
        m_na_engine.update();
    if (m_show_na)
        m_na_widget->draw("Network Analyzer", &m_show_na);
    if (m_show_receiver_requirements) {
        ReceiverRequirementsConfig measurement_config;
        if (m_receiver_requirements.config && m_receiver_requirements.invalid_reason.empty())
            measurement_config = *m_receiver_requirements.config;
        // The engine can auto-select a lone tone for UI-free callers. The
        // app's applied selector is authoritative: a missing selection must
        // remain incomplete until the editor applies its preselection.
        if (measurement_config.output_power &&
            !measurement_config.measurement_conditions.output_reference_tone_frequency_Hz)
            measurement_config.measurement_conditions.output_reference_tone_frequency_Hz = -1.0;
        m_receiver_performance_engine.update(measurement_config, m_na_engine.pointAPin(),
                                             m_na_engine.pointBPin(),
                                             m_na_engine.sweepFrequencies());

        std::vector<ReceiverGeneratorToneOption> source_tones;
        const int source_node_id = m_circuit_runtime.graph().nodeIdForPin(m_na_engine.pointAPin());
        if (IComponentEngine *source = m_circuit_runtime.components().find(source_node_id);
            source && source->type_name() == "generator") {
            const auto *generator = static_cast<const SignalGeneratorEngine *>(source);
            source_tones.reserve(generator->tones().size());
            for (const auto &tone : generator->tones())
                source_tones.push_back({tone.freq_Hz, tone.power_dBm});
        }
        const auto &measurements = m_receiver_performance_engine.measurements();
        const auto result = evaluateReceiverRequirements(
            m_receiver_requirements, m_na_engine.startFrequency(), m_na_engine.stopFrequency(),
            m_na_engine.sweepFrequencies(), m_na_engine.gainDb(), m_na_engine.noiseFigureDb(),
            measurements.output_power_dBm, measurements.iip3_dBm);
        m_receiver_requirements_widget->draw("Receiver Requirements", &m_show_receiver_requirements,
                                             m_receiver_requirements, result, source_tones);
    }

    if (m_show_power_meter)
        m_power_meter_widget->draw("Power Meter", &m_show_power_meter);

    m_pfb_views.draw();

    for (size_t i = 0; i < m_generator_widgets.size(); ++i) {
        m_generator_widgets[i]->draw("Generators");
    }

    if (m_show_properties && m_inspector_panel)
        m_inspector_panel->draw("Properties", &m_show_properties);

    if (m_show_library && m_library_browser) {
        m_library_browser->draw("Component Library", &m_show_library);
    }
    if (m_show_calculator && m_calculator_widget) {
        m_calculator_widget->draw("Filter Calculator", &m_show_calculator);
    }
    if (m_show_test_flow) {
        m_test_flow_widget->draw(
            "Test Flow", &m_show_test_flow, [this]() { openTestFlowDialog(); },
            [this]() { exportTestFlowDialog(); }, [this]() { saveTestFlowDialog(); });
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

    // Last so a Trust request raised from the Extensions panel or a refused
    // run opens within the same frame.
    drawExtensionTrustPrompt();
}

RfSimulatorApp::~RfSimulatorApp() {
    m_state.saveBool("WindowState", "Log", m_show_log);
    m_state.saveBool("WindowState", "SpectrumAnalyzer", m_show_spectrum);
    m_state.saveBool("WindowState", "NetworkAnalyzer", m_show_na);
    m_state.saveBool("WindowState", "PowerMeter", m_show_power_meter);
    m_state.saveBool("WindowState", "ReceiverRequirements", m_show_receiver_requirements);
    m_state.saveBool("WindowState", "Properties", m_show_properties);
    m_pfb_views.saveVisibility(m_circuit_runtime.components(), m_state);
    m_state.saveBool("WindowState", "NodeEditor", m_show_node_editor);
    m_state.saveBool("WindowState", "Help", m_show_help);
    m_state.saveBool("WindowState", "FilterCalculator", m_show_calculator);
    m_state.saveBool("WindowState", "TestFlow", m_show_test_flow);
}
