#include "core.h"
#include "app.h"

int main() {
	RfSimulatorCore core;
	RfSimulatorApp app;
	core.Run([&app]() {app.draw_ui(); });
	return 0;
}