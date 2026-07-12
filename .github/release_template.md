# v0.5.1 — executable rename, window title, CI release packaging

## Highlights

The main binary is now named `tiny-rf-simulator` (was `main`), window title updated to match, and tag pushes automatically produce platform-specific release archives on GitHub.

## Features

- **Executable rename:** binary target renamed `main` → `tiny-rf-simulator` (`.exe` on Windows)
- **Window title:** "RF Simulator GUI" → "Tiny RF Simulator"

## Documentation

- Updated all usage examples in README, quickstart guide, and build-runbook to use the new executable name
- release_template.md restored as a template for future releases

## Internal

- **CI release pipeline:** on `v*` tag push, the workflow now packages `tiny-rf-simulator` (+ MinGW DLLs on Windows) and uploads as GitHub Release assets
- **Version bump:** 0.5.0 → 0.5.1
