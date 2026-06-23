#pragma once

#include "adc_engine.h"
#include "amplifier_engine.h"
#include "inspector_panel.h"
#include "ideal_filter_engine.h"
#include "iq_plot_widget.h"
#include "logging_widget.h"
#include "mixer_engine.h"
#include "node_graph_engine.h"
#include "node_graph_widget.h"
#include "pfb_channelizer_engine.h"
#include "pfb_channelizer_widget.h"
#include "s_param_engine.h"
#include "session_state.h"
#include "signal_generator_engine.h"
#include "signal_generator_widget.h"
#include "spectrum_analyzer_engine.h"
#include "spectrum_analyzer_widget.h"
#include "splitter_engine.h"
#include "view_manager.h"
#include <memory>
#include <vector>
#include "component_registry.h"

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
    SessionState m_state;
    ~RfSimulatorApp();

    // Project save/load
    void saveProject(const std::string& path);
    void loadProject(const std::string& path);
    void newProject();
    bool isDirty() const { return m_dirty; }
    size_t componentCount() const { return m_components.size(); }
    std::string m_current_project_path;
    void markDirty() { m_dirty = true; }
    bool m_dirty = false;
    void testMakeDirty();
    void openFileDialog();
    void saveFileDialog();

  private:
    void load_window_states();
    NodeGraphEngine m_graph_engine;
    ViewManager m_view_manager;
    SpectrumAnalyzerEngine m_spectrum_engine;
    std::unique_ptr<SpectrumAnalyzerWidget> m_spectrum_widget;
    std::unique_ptr<NodeGraphWidget> m_graph_widget;

    std::vector<std::unique_ptr<SignalGeneratorWidget>> m_generator_widgets;
    std::vector<std::unique_ptr<IQPlotWidget>> m_iq_widgets;
    std::vector<bool> m_show_iq_pfbs;
    std::vector<std::unique_ptr<PFBChannelizerWidget>> m_pfb_grid_widgets;
    std::vector<bool> m_show_pfb_grids;
    std::unique_ptr<InspectorPanel> m_inspector_panel;

    ComponentRegistry m_components;
    int m_next_component_id = 100;
    PendingAction m_pending_action = PendingAction::None;
    bool m_show_unsaved_dialog = false;
};
