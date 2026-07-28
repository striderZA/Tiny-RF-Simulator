#pragma once

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "attenuator_engine.h"
#include "component_form_model.h"
#include "component_form_widget.h"

#include "component_library.h"
#include "component_registry.h"
#include "equalizer_engine.h"
#include "component_type_registry.h"

#include "help_widget.h"
#include "ideal_filter_engine.h"
#include "inspector_panel.h"
#include "iq_plot_widget.h"
#include "layout_manager.h"
#include "library_browser_widget.h"
#include "logging_widget.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_channelizer_widget.h"
#include "session_state.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "splitter_engine.h"
#include "view_manager.h"
#include <memory>
#include <optional>
#include <vector>
enum class PendingAction { None, New, Open, Exit };

class RfSimulatorApp {
  public:
    RfSimulatorApp();
    void draw_ui();
    void update_dsp();

    LoggingWidget m_log_widget;
    bool m_show_log = true;
    bool m_show_spectrum = true;
    bool m_show_properties = true;
    bool m_show_node_editor = true;
    bool m_show_help = false;
    HelpWidget m_help_widget;
    LayoutManager m_layout_manager;
    bool m_show_save_layout_dialog = false;
    bool m_show_manage_layouts_dialog = false;
    char m_layout_name_buf[128] = {};
    std::string m_rename_target;
    char m_rename_buf[128] = {};
    SessionState m_state;
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

    void openFileDialog();
    void saveFileDialog();

  private:
    void load_window_states();
    void duplicateComponent(int graph_node_id);
    void openNewComponentForm(const std::string &type);
    void openEditComponentForm(const ComponentDefinition &def);
    void drawComponentFormModal();
    bool saveComponentForm();
    NodeGraphEngine m_graph_engine;
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<NodeGraphWidget> m_graph_widget;

    std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
    std::vector<std::unique_ptr<IQPlotWidget>> m_iq_widgets;
    std::vector<bool> m_show_iq_pfbs;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> m_pfb_grid_widgets;
    ComponentLibrary m_library;
    std::unique_ptr<LibraryBrowserWidget> m_library_browser;
    bool m_show_library = false;
    bool m_show_component_form = false;
    bool m_component_form_is_edit = false;
    std::string m_component_form_destination_root;
    std::unique_ptr<ComponentFormModel> m_component_form_model;
    std::unique_ptr<ComponentFormWidget> m_component_form_widget;
    std::string m_component_form_error;
    std::vector<bool> m_show_pfb_grids;
    std::unique_ptr<InspectorPanel> m_inspector_panel;

    ComponentRegistry m_components;
    int m_next_component_id = 100;
    PendingAction m_pending_action = PendingAction::None;
    bool m_show_unsaved_dialog = false;
};
