#include "app.h"
#include "core.h"

int main() {
    RfSimulatorCore core;
    RfSimulatorApp app;
    core.Run([&app]() {
        app.draw_ui();
        app.update_dsp();
    });
    return 0;
}
