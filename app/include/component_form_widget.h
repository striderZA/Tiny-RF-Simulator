// app/include/component_form_widget.h
#pragma once

#include "component_form_model.h"

class ComponentLibrary;

class ComponentFormWidget {
  public:
    explicit ComponentFormWidget(ComponentFormModel &model);

    // Renders the fields the model shows for the current parameters (see
    // ComponentFormModel::fieldLabel()); returns true only on the frame Save is
    // clicked (disabled while validate() reports issues). A shown field renders
    // its first issue beneath it; whole-definition issues and those of a hidden
    // field render under Save. A shown field's further issues appear only in the
    // issue count beside Save.
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
