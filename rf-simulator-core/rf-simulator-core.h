// rf-simulator-core.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <functional>
#include <memory>

class RfSimulatorCore {
public:
	RfSimulatorCore();
	~RfSimulatorCore();

	void Run(const std::function<void()>& onGui);
private:
	bool Initialize();
	void Shutdown();
	void MainLoop(const std::function<void()>& onGui);

	struct Impl;
	//std::unique_ptr<Impl> p_impl;
	Impl* p_impl;
};