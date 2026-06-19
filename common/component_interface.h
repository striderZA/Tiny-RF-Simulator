#pragma once

#include "signal_node.h"
#include <string>

class IComponentEngine {
public:
    virtual ~IComponentEngine() = default;
    virtual int id() const = 0;
    virtual int graphNodeId() const = 0;
    virtual int outputPinId() const = 0;
    virtual std::string hoverSummary() const = 0;
    virtual SignalNode& node() = 0;
    virtual const SignalNode& node() const = 0;
    virtual void update(double dt) = 0;

    virtual int inputPinId() const { return -1; }

    // Multi-pin accessors (default: return -1 for single-pin engines)
    virtual int inputPinId(int /*port*/) const { return -1; }
    virtual int outputPinId(int /*port*/) const { return -1; }

    // Pin count (default: 1/1 for legacy single-pin engines)
    virtual int numInputPins() const { return 1; }
    virtual int numOutputPins() const { return 1; }
};
