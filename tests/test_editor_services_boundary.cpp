#if __has_include("imgui.h") || __has_include("imnodes.h") ||                                      \
                                              __has_include("implot.h") ||                         \
                                                            __has_include("GLFW/glfw3.h")
#error "simulator::editor_services must not expose UI include paths"
#endif

#include "component_type_registry.h"
#include "editor_commands.h"
#include "graph_editor_actions.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("UI-free editor services create and connect components", "[editor_services]") {
    CircuitRuntime runtime;
    GraphEditorActions actions(runtime);
    EditorCommands commands(runtime, actions);

    const auto *generator_type = ComponentTypeRegistry::instance().find("generator");
    const auto *amplifier_type = ComponentTypeRegistry::instance().find("amplifier");
    REQUIRE(generator_type != nullptr);
    REQUIRE(amplifier_type != nullptr);

    auto *generator = commands.createComponent(generator_type->create);
    auto *amplifier = commands.createComponent(amplifier_type->create);
    REQUIRE(generator != nullptr);
    REQUIRE(amplifier != nullptr);
    REQUIRE(commands.connect(generator->outputPinId(), amplifier->inputPinId()).has_value());
    REQUIRE(commands.revision() == 3);
}
