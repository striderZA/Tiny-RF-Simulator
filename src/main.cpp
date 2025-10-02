#include "RfSimulatorCore.h"
#include "RfSimulatorApp.h"

int main() {
	RfSimulatorCore core;
	RfSimulatorApp app;
	core.Run([&app]() {app.onGui(); });
	return 0;
}