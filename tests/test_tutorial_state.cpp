#include "tutorial_state.h"
#include "tutorial_steps.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

TEST_CASE("TutorialState derives a non-empty exe-relative marker path", "[tutorial]") {
    TutorialState state;
    std::string path = state.markerPath();

    REQUIRE_FALSE(path.empty());
    REQUIRE(std::filesystem::path(path).filename().string() == ".tutorial_completed");
    REQUIRE_FALSE(std::filesystem::path(path).parent_path().empty());
}

TEST_CASE("TutorialState completed()/markCompleted() round-trip", "[tutorial]") {
    TutorialState state;
    // Clean slate — the marker lives next to the test executable.
    std::filesystem::remove(state.markerPath());
    REQUIRE_FALSE(state.completed());

    state.markCompleted();
    REQUIRE(state.completed());

    // markCompleted() is idempotent.
    state.markCompleted();
    REQUIRE(state.completed());

    std::filesystem::remove(state.markerPath());
    REQUIRE_FALSE(state.completed());
}

TEST_CASE("TutorialState catalog is non-empty and fully addressable", "[tutorial]") {
    REQUIRE_FALSE(tutorialSteps().empty());
    for (const auto &step : tutorialSteps()) {
        REQUIRE(step.title != nullptr);
        REQUIRE(step.instruction != nullptr);
    }
    // Every target except None resolves to a window title.
    REQUIRE(tutorialTargetWindowTitle(TutorialTarget::None) == nullptr);
    REQUIRE(std::string(tutorialTargetWindowTitle(TutorialTarget::NodeEditor)) == "Node Editor");
    REQUIRE(std::string(tutorialTargetWindowTitle(TutorialTarget::ComponentLibrary)) ==
            "Component Library");
    REQUIRE(std::string(tutorialTargetWindowTitle(TutorialTarget::Properties)) == "Properties");
    REQUIRE(std::string(tutorialTargetWindowTitle(TutorialTarget::SpectrumAnalyzer)) ==
            "Spectrum Analyzer");
}

TEST_CASE("TutorialState is inactive until started", "[tutorial]") {
    TutorialState state;
    REQUIRE_FALSE(state.isActive());
    state.start();
    REQUIRE(state.isActive());
    REQUIRE(state.atFirstStep());
    REQUIRE(state.stepIndex() == 0);
    REQUIRE(state.stepCount() == static_cast<int>(tutorialSteps().size()));
}

TEST_CASE("TutorialState navigation stays within bounds", "[tutorial]") {
    TutorialState state;
    std::filesystem::remove(state.markerPath());
    state.start();

    SECTION("back() at the first step is a no-op") {
        state.back();
        REQUIRE(state.atFirstStep());
        REQUIRE(state.stepIndex() == 0);
        REQUIRE(state.isActive());
    }

    SECTION("next() walks every step, then finishes") {
        for (int i = 0; i < state.stepCount() - 1; ++i) {
            REQUIRE_FALSE(state.atLastStep());
            state.next();
            REQUIRE(state.stepIndex() == i + 1);
            REQUIRE(state.isActive());
        }
        REQUIRE(state.atLastStep());

        // One more next() finishes: marks completed and deactivates.
        state.next();
        REQUIRE_FALSE(state.isActive());
        REQUIRE(state.completed());
    }

    SECTION("next() then back() returns to the previous step") {
        state.next();
        REQUIRE(state.stepIndex() == 1);
        state.back();
        REQUIRE(state.stepIndex() == 0);
    }

    SECTION("skipToLast() jumps to the final step without finishing") {
        state.skipToLast();
        REQUIRE(state.atLastStep());
        REQUIRE(state.isActive());
        REQUIRE_FALSE(state.completed());

        // Already at the end — skipping again must not overshoot.
        state.skipToLast();
        REQUIRE(state.stepIndex() == state.stepCount() - 1);
    }

    SECTION("exit() deactivates without marking completed") {
        state.next();
        state.exit();
        REQUIRE_FALSE(state.isActive());
        REQUIRE_FALSE(state.completed());
    }

    SECTION("currentStep() matches the catalog entry at every index") {
        for (int i = 0; i < state.stepCount(); ++i) {
            REQUIRE(state.currentStep().title == tutorialSteps()[i].title);
            if (!state.atLastStep())
                state.next();
        }
    }

    SECTION("start() rewinds a partially-walked tutorial") {
        state.next();
        state.next();
        REQUIRE(state.stepIndex() == 2);
        state.start();
        REQUIRE(state.atFirstStep());
        REQUIRE(state.isActive());
    }

    std::filesystem::remove(state.markerPath());
}
