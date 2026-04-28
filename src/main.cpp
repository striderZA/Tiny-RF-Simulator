#include "app.h"
#include "core.h"
#include <imnodes.h>

int main() {
    RfSimulatorCore core;
    ImNodes::CreateContext();
    RfSimulatorApp app;
    core.Run([&app]() {
        app.draw_ui();
        app.update_dsp();
    });
    ImNodes::DestroyContext();
    return 0;
}
