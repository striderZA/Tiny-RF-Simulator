#pragma once

#include <algorithm>

#include "component_engine_base.h"
#include "node_graph_engine.h"
#include "signal_node.h"

class AdcEngine : public ComponentEngineBase {
  public:
    AdcEngine(int id, NodeGraphEngine &graph);

    std::string_view type_name() const override { return "adc"; }
    std::string hoverSummary() const override;

    void update(double dt) override;
    nlohmann::json serialize() const override;
    void deserialize(const nlohmann::json &) override;

    double fs_Hz() const { return m_fs_Hz; }
    void setFs_Hz(double fs) {
        double clamped = std::max(fs, 1.0);
        if (clamped != m_fs_Hz) {
            m_fs_Hz = clamped;
            m_dirty = true;
        }
    }
    double nsd_dBm_per_Hz() const { return m_nsd_dBm_per_Hz; }
    void setNsd_dBm_per_Hz(double nsd) {
        if (nsd != m_nsd_dBm_per_Hz) {
            m_nsd_dBm_per_Hz = nsd;
            m_dirty = true;
        }
    }
    int decimation() const { return m_decimation; }
    void setDecimation(int decimation) {
        static const int supported[] = {1, 2, 4, 8};
        int nearest = supported[0];
        int best_dist = std::abs(decimation - supported[0]);
        for (int s : supported) {
            int dist = std::abs(decimation - s);
            if (dist < best_dist) { // strict < keeps the lower value on ties
                nearest = s;
                best_dist = dist;
            }
        }
        if (nearest != m_decimation) {
            m_decimation = nearest;
            m_dirty = true;
        }
    }
    double ncoFsFraction() const { return m_nco_fs_fraction; }
    void setNcoFsFraction(double fraction) {
        double clamped = std::clamp(fraction, -0.5, 0.5);
        if (clamped != m_nco_fs_fraction) {
            m_nco_fs_fraction = clamped;
            m_dirty = true;
        }
    }

  private:
    double m_fs_Hz = 1e9;
    double m_nsd_dBm_per_Hz = -155.0;
    int m_decimation = 2;
    double m_nco_fs_fraction = 0.25;
};
