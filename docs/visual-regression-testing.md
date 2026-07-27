# Visual Regression Testing

This document explains how to manage visual regression baselines for the RF Simulator UI tests.

## Overview

Visual regression tests capture screenshots of key UI states and compare them against reference baselines. Tests are located in `test_engine/ui_tests.cpp` and baselines are stored in `test_engine/baselines/`.

## Baseline Tests

The following visual baselines are captured:

1. **default_graph** - Empty node editor with default generator and amplifier
2. **single_node_selected** - Amplifier node selected in the editor
3. **connected_chain** - Generator → Amplifier → Mixer chain
4. **group_collapsed** - Subcircuit group in collapsed state
5. **inspector_populated** - Properties panel showing generator settings
6. **full_ui** - Complete application viewport

## Generating Baselines

### First-time setup

Baselines are automatically generated on the first test run. Simply run:

```bash
# Build the test executable
cmake --build build --target test_ui

# Run tests (baselines will be created in test_engine/baselines/)
cd build
./bin/test_ui
```

### Regenerating baselines

After intentional UI changes (theme updates, layout modifications, new features), regenerate baselines:

```bash
# Set environment variable to force baseline updates
export UPDATE_BASELINES=1

# Run tests
cd build
./bin/test_ui

# Unset when done
unset UPDATE_BASELINES
```

Or in a single command:

```bash
UPDATE_BASELINES=1 ./build/bin/test_ui
```

## CI Integration

Visual regression tests run automatically in CI on all platforms:

- **Linux**: Uses Xvfb for headless rendering
- **Windows**: Uses Mesa software rendering
- **macOS**: Native rendering (when added)

### Test timeout

All UI tests have a 60-second timeout to prevent hung tests from blocking CI.

### Failure artifacts

When visual regression tests fail, CI automatically uploads:

- `build/output/` - Screenshot captures from failed tests
- `build/Testing/` - Test logs and detailed output

Download these artifacts from the GitHub Actions run to investigate failures.

## Tolerance

Visual comparisons allow a tolerance of 10/255 per color channel to account for:

- Font rendering differences across platforms
- Anti-aliasing variations
- Minor timing-dependent rendering differences

The full UI baseline uses a higher tolerance (15/255) due to complex rendering.

## Troubleshooting

### Tests fail with "diff > tolerance"

1. Check if the UI change was intentional
2. If yes, regenerate baselines with `UPDATE_BASELINES=1`
3. If no, investigate the regression by examining failure artifacts

### Capture fails with "disabled by IMGUI_TEST_ENGINE_ENABLE_CAPTURE=0"

This should not happen in the current setup. The CMakeLists.txt explicitly enables capture:

```cmake
target_compile_definitions(imgui_test_engine PRIVATE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
    IMGUI_TEST_ENGINE_ENABLE_CAPTURE=1
)
```

If you see this error, verify the CMake configuration is up to date.

### Platform-specific differences

If baselines differ significantly between platforms:

1. Generate platform-specific baselines by running tests on each platform with `UPDATE_BASELINES=1`
2. Consider using conditional baseline directories (not yet implemented)
3. Increase tolerance if differences are minor and expected

## Adding new baseline tests

To add a new visual regression test:

1. Add test case in `test_engine/ui_tests.cpp`:

```cpp
t = IM_REGISTER_TEST(e, "rf_simulator", "visual_baseline_my_feature");
t->TestFunc = [](ImGuiTestContext *ctx) {
    // Set up UI state
    ctx->WindowFocus("Node Editor");
    ctx->Yield(4);
    
    // Capture and compare
    int diff = ScreenshotHelper::verifyBaseline(ctx, "Node Editor", "my_feature");
    IM_CHECK(diff >= 0 && diff <= 10);
};
```

2. Run tests with `UPDATE_BASELINES=1` to generate the baseline
3. Commit the new baseline file in `test_engine/baselines/`

## Architecture

- **ScreenshotHelper**: Utility class in `test_engine/test_helpers.h/cpp`
  - `captureWindow()`: Capture a specific ImGui window
  - `captureFullViewport()`: Capture the entire application
  - `verifyBaseline()`: Compare against baseline or create if missing
  - `compareImages()`: Pixel-by-pixel comparison using stb_image

- **ImGui Test Engine**: Provides screenshot capture via `ImGuiCaptureContext`
  - Uses `glReadPixels` for OpenGL framebuffer capture
  - Saves PNG files via stb_image_write
  - Supports instant capture for synchronous testing

## Future improvements

- Platform-specific baseline directories
- Diff image generation (highlight changed pixels)
- Automated baseline update PRs
- Visual regression dashboard
