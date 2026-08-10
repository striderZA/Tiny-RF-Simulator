#pragma once

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "component_form_model.h"
#include "component_form_widget.h"

#include "component_library.h"
#include "component_registry.h"
#include "component_type_registry.h"
#include "equalizer_engine.h"
#include "extension_manager.h"
#include "external_tool_runner.h"

#include "help_widget.h"
#include "ideal_filter_engine.h"
#include "inspector_panel.h"
#include "iq_plot_widget.h"
#include "layout_manager.h"
#include "library_browser_widget.h"
#include "logging_widget.h"
#include "mixer_engine.h"
#include "network_analyzer_engine.h"
#include "network_analyzer_widget.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_channelizer_widget.h"
#include "pfb_view_manager.h"
#include "project_serializer.h"
#include "session_state.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "splitter_engine.h"
#include "tutorial_state.h"
#include "tutorial_widget.h"
#include "view_manager.h"
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
enum class PendingAction { None, New, Open, Exit, Tutorial };

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();
    void refreshExtensions();
    void drawExtensionsPanel();
    std::vector<ExtensionMenuEntry> externalToolActions(const ExtensionManifest &manifest) const;
    void runExternalTool(const ExtensionManifest &manifest, std::string_view action_label = {});
    LoggingWidget m_log_widget;
    bool m_show_log = true;
    bool m_show_spectrum = true;
    bool m_show_na = false;
    bool m_show_properties = true;
    bool m_show_node_editor = true;
    bool m_show_help = false;
    HelpWidget m_help_widget;
    bool m_show_tutorial = false;
    // Set on construction when no completion marker exists. Public so the UI
    // test harness can suppress the blocking modal (see test_engine/ui_tests.cpp).
    bool m_show_tutorial_first_run_prompt = false;
    TutorialWidget m_tutorial_widget;
    LayoutManager m_layout_manager;
    bool m_show_save_layout_dialog = false;
    bool m_show_manage_layouts_dialog = false;
    char m_layout_name_buf[128] = {};
    std::string m_rename_target;
    char m_rename_buf[128] = {};
    ExtensionManager m_extension_manager;
    ExternalToolRunner m_external_tool_runner;
    bool m_show_extensions = false;
    std::string m_extension_result_message;

    SessionState m_state;
    TutorialState m_tutorial_state;
    ~RfSimulatorApp();

    // Project save/load
    void saveProject(const std::string &path);
    void loadProject(const std::string &path);
    void newProject();
    bool isDirty() const { return m_dirty; }
    size_t componentCount() const { return m_components.size(); }
    std::string m_current_project_path;
    void markDirty();
    bool m_dirty = false;
    void testMakeDirty();
    // Test helpers exposed for project file round-trip tests
    NodeGraphEngine &testGraphEngine() { return m_graph_engine; }
    ComponentRegistry &testComponents() { return m_components; }
    NodeGraphWidget &testGraphWidget() { return *m_graph_widget; }
    LayoutManager &testLayoutManager() { return m_layout_manager; }
    TutorialState &testTutorialState() { return m_tutorial_state; }
    ExtensionManager &testExtensionManager() { return m_extension_manager; }
    const std::string &testExtensionResultMessage() const { return m_extension_result_message; }

    void openFileDialog();
    void saveFileDialog();

  private:
    void load_window_states();
    // Runs the unsaved-changes guard, then starts the tutorial (directly if the
    // project is clean, otherwise via PendingAction::Tutorial).
    void requestTutorial();
    // Resets to a fresh seeded sandbox and activates the walkthrough.
    void startTutorial();
    void rewireInputs();
    void duplicateComponent(int graph_node_id);
    void addComponent(const ComponentTypeDescriptor *desc, ImVec2 pos);
    void openNewComponentForm(const std::string &type);
    void openEditComponentForm(const ComponentDefinition &def);
    void drawComponentFormModal();
    bool saveComponentForm();

    // --- Network Analyzer host adapter --------------------------------------
    // The engine lives in the DSP-engines layer below app/ and never sees app
    // types; RfSimulatorApp implements its two injected lookups (see
    // network_analyzer_engine.h's layering comment). componentForNode wraps
    // ComponentRegistry::find; beginScratchPass hands out one private,
    // throwaway scratch graph+registry per measurement pass whose clones are
    // destroyed with it (RAII), so a pass never touches the real graph/registry.
    class NaScratch;
    class NaHost final : public INetworkAnalyzerHost {
      public:
        explicit NaHost(ComponentRegistry &components);
        IComponentEngine *componentForNode(int graph_node_id) const override;
        std::unique_ptr<INetworkAnalyzerScratch> beginScratchPass() const override;

      private:
        ComponentRegistry &m_components;
    };
    class NaScratch final : public INetworkAnalyzerScratch {
      public:
        NaScratch();
        IComponentEngine *createClone(std::string_view type, int id) override;

      private:
        NodeGraphEngine m_graph;
        ViewManager m_view;
        ComponentRegistry m_registry; // constructed with (m_graph, m_view)
    };

    NodeGraphEngine m_graph_engine;
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<NetworkAnalyzerWidget> m_na_widget;
    std::unique_ptr<NodeGraphWidget> m_graph_widget;

    std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
    ComponentLibrary m_library;
    std::unique_ptr<LibraryBrowserWidget> m_library_browser;
    bool m_show_library = false;
    bool m_show_component_form = false;
    bool m_component_form_is_edit = false;
    std::string m_component_form_destination_root;
    std::unique_ptr<ComponentFormModel> m_component_form_model;
    std::unique_ptr<ComponentFormWidget> m_component_form_widget;
    std::string m_component_form_error;
    std::unique_ptr<InspectorPanel> m_inspector_panel;

    ComponentRegistry m_components;
    // Declared after m_components so the manager (and its widget references to
    // engines) is destroyed before the engines themselves.
    PFBViewManager m_pfb_views;
    // Adapter implementing the engine's injected lookups; declared after
    // m_components (which it references) and before m_na_engine (which holds a
    // reference to it — the adapter must outlive the engine).
    NaHost m_na_host{m_components};
    // Singleton Network Analyzer instrument engine — a plain value member
    // exactly like m_spectrum_engine (not an IComponentEngine, no registry
    // row, no graph node).
    NetworkAnalyzerEngine m_na_engine{m_graph_engine, m_na_host};
    // Owns .rfsim save/load/new; declared after m_graph_widget and m_pfb_views
    // so it is destroyed before both (it holds references to them).
    std::unique_ptr<ProjectSerializer> m_serializer;
    int m_next_component_id = 100;
    PendingAction m_pending_action = PendingAction::None;
    bool m_show_unsaved_dialog = false;
};
