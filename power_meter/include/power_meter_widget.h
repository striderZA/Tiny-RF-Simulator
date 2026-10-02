#pragma once

#include "power_meter_engine.h"

class NodeGraphEngine;

class PowerMeterWidget {
  public:
    PowerMeterWidget(PowerMeterEngine &engine, const NodeGraphEngine &graph);

    void draw(const char *title, bool *p_open = nullptr);
    int sourcePin() const { return m_source_pin; }
    void setSourcePin(int pin_id) { m_source_pin = pin_id; }
    void clearSource() { m_source_pin = -1; }

  private:
    PowerMeterEngine &m_engine;
    const NodeGraphEngine &m_graph;
    int m_source_pin = -1;
};
