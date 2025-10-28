#pragma once

#include <vector>
#include <memory>
#include <imnodes.h>
#include "signal_generator.h"
#include "spectrum_analyzer.h"

enum class InputSignals : int {
	G0 = 0,
	G1,
	G2,
	G3
};

struct RfNode {
	int id;
	std::string name;
	double gain_dB;
	int inputPin;
	int outputPin;
};

struct RfLink {
	int id;
	int startAttribute, endAttribute;
};

class RfNodeEditor {
public:
	void initialize() {
		m_nodes.push_back({ 0, "Source", 0.0f, -1, 1 });
		m_nodes.push_back({ 0, "Attenuator", -10.0f, 3, 4 });
		m_nodes.push_back({ 5, "Load", 0.0f, 6, -1 });
	}

	void shutdown() {
	}

	void draw() {
		ImNodes::BeginNodeEditor();
		for (auto& node : m_nodes) {
		}
		ImNodes::EndNodeEditor();
	}

private:
	std::vector<RfNode> m_nodes;
	std::vector<RfLink> m_links;
	int m_nextLinkId = 0;
};

class RfSimulatorApp {
public:
	RfSimulatorApp();
	void onGui();

private:
	bool m_enable_log;
	SpectrumAnalyzer m_spectrum_analyzer;
	std::vector<std::unique_ptr<SignalGenerator>> m_signal_generators;
	RfNodeEditor m_node_editor;
};
