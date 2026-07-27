# help/AGENTS.md

## Purpose
Help window module providing a modeless, dockable ImGui panel with data-driven quick reference content for the simulator.

## Ownership
- `HelpWidget` — renders the "How to Use" help panel with collapsible sections

## Local Contracts
- `draw(title, p_open)` — standard ImGui panel draw signature matching other widgets
- Content is defined as a static array of `HelpSection` structs in `help_widget.cpp`
- Each section has a title and up to 8 bullet points

## Work Guidance
- To add new help topics: append a new `HelpSection` entry to the `sections[]` array in `help_widget.cpp`
- Keep bullets concise (one line each)
- Content is embedded as string literals (no external file loading)

## Verification
- Build: `cmake --build build --target help_widget`
- Integration: `cmake --build build --target tiny-rf-simulator`

## Child DOX Index
*(none)*
