// app/include/component_form_widget.h
#pragma once

#include "component_form_model.h"

class ComponentLibrary;

class ComponentFormWidget {
  public:
    explicit ComponentFormWidget(ComponentFormModel &model);

    // Renders all fields; returns true only on the frame Save is clicked
    // (disabled while validate() reports issues). Renders inline issue text
    // under each offending field.
    bool draw(const ComponentLibrary &library);

  private:
    ComponentFormModel *m_model;
    char m_part_number_buf[128] = {};
    char m_manufacturer_buf[128] = {};
    char m_description_buf[256] = {};
    char m_notes_buf[512] = {};
    bool m_buffers_initialized = false;
    void initBuffersOnce();
};
