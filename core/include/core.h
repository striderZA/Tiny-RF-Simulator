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
	std::unique_ptr<Impl> p_impl;
};
