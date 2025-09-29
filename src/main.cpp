#include "rf-simulator-core.h"
#include "rf-simulator-app.h"

int main() {
	RfSimulatorCore core;
	RfSimulatorApp app;
	core.Run([&app]() {app.onGui(); });
	return 0;
}