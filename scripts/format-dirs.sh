#!/usr/bin/env bash
# Single source of truth for the C++ directories clang-format covers.
#
# Sourced (not executed) by:
#   - scripts/format.sh        (local reformat / --check)
#   - .githooks/pre-commit     (commit gate)
#   - .github/workflows/release.yml's format job runs
#     `bash scripts/format.sh --check --all`, so it shares this list too.
#
# Add new C++ module directories here only.
FORMAT_DIRS=(src app core common tests test_engine signal_generator amplifier spectrum_analyzer equalizer node_graph splitter mixer adc coax pfb_channelizer iq_plot network_analyzer ideal_filter attenuator combiner rf_switch rf_switch_2to1 power_meter touchstone help layout tutorial logging test_flow)
