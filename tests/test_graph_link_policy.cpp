// Direct coverage of the single physical-link rule shared by the node canvas,
// project-load restoration, and the test-flow runner (common/graph_link_policy.h).
#include "graph_link_policy.h"
#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace {

class FakeEngine final : public IComponentEngine {
  public:
    FakeEngine(std::string_view type, int input_pin, int output_pin)
        : m_type(type), m_input_pin(input_pin), m_output_pin(output_pin) {}

    int id() const override { return 0; }
    int graphNodeId() const override { return 0; }
    int outputPinId() const override { return m_output_pin; }
    int outputPinId(int port) const override { return port == 0 ? m_output_pin : -1; }
    int inputPinId() const override { return m_input_pin; }
    int inputPinId(int port) const override { return port == 0 ? m_input_pin : -1; }
    std::string hoverSummary() const override { return {}; }
    SignalNode &node() override { return m_node; }
    const SignalNode &node() const override { return m_node; }
    void update(double) override {}
    std::string_view type_name() const override { return m_type; }

  private:
    std::string_view m_type;
    int m_input_pin;
    int m_output_pin;
    SignalNode m_node;
};

} // namespace

TEST_CASE("graph link policy leaves non-PFB targets unrestricted", "[node_graph][link_policy]") {
    FakeEngine amplifier("amplifier", 3, 4);
    FakeEngine adc("adc", 0, 7);

    CHECK(graphLinkAllowed(nullptr, &amplifier, 0, 3));
    CHECK(graphLinkAllowed(&adc, &amplifier, 7, 3));
    // The policy does not police non-PFB ports; the graph editor owns that.
    CHECK(graphLinkAllowed(&adc, &amplifier, 99, 99));
}

TEST_CASE("graph link policy refuses PFB targets without an ADC source",
          "[node_graph][link_policy]") {
    FakeEngine pfb("pfb", 42, 0);
    FakeEngine amplifier("amplifier", 0, 4);
    FakeEngine generator("generator", 0, 0);

    CHECK_FALSE(graphLinkAllowed(nullptr, &pfb, 0, 42));
    CHECK_FALSE(graphLinkAllowed(&amplifier, &pfb, 4, 42));
    CHECK_FALSE(graphLinkAllowed(&generator, &pfb, 0, 42));
}

TEST_CASE("graph link policy only lets an ADC feed a PFB on its real pins",
          "[node_graph][link_policy]") {
    FakeEngine adc("adc", 0, 7);
    FakeEngine pfb("pfb", 42, 0);

    // The ADC's output pin must be the link's start and the PFB's input pin its end.
    CHECK(graphLinkAllowed(&adc, &pfb, 7, 42));
    CHECK_FALSE(graphLinkAllowed(&adc, &pfb, 8, 42));
    CHECK_FALSE(graphLinkAllowed(&adc, &pfb, 7, 43));
}
