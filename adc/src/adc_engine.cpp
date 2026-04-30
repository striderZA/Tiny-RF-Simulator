#include "adc_engine.h"
AdcEngine::AdcEngine(int id, NodeGraphEngine& graph) {}
void AdcEngine::update(double dt) {}
SignalNode& AdcEngine::node() { static SignalNode s; return s; }
