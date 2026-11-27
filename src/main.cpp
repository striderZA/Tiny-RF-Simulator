#include "app.h"
#include "core.h"
#include <imnodes.h>

int main() {
    RfSimulatorCore core;
    ImNodes::CreateContext();
    RfSimulatorApp app;
    app.startAgentServerIfEnabled();
    core.Run(
        [&app]() {
            app.update_dsp();
            app.draw_ui();
        },
        [&app]() { app.applyPendingUiFont(); });
    ImNodes::DestroyContext();
    return 0;
}
