# Settings

The **Settings** menu sits between **Tools** and **Help** in the menu bar. It holds the simulator's preferences. The first one is the interface font.

## Choose a UI font

1. Choose **Settings > UI Font...** to open the Settings window.
2. Pick a family from the **UI font** list. The list holds **Default (built-in)** and every monospaced font the system has installed, sorted by name.
3. Check the **Preview** section. It shows the name of the font in use and a sample line set in that font.

The font applies to the whole interface as soon as you pick it. Pick **Default (built-in)** to return to the font the simulator ships with.

Only monospaced fonts are listed, so the numbers in tables and readouts line up in columns. Each family appears once, even when the system reports several styles of it.

## What is saved

The choice is saved with the simulator's settings, in `app.ini` beside the executable on Windows, so the next start uses the same font. Your choice is kept even when the font cannot be used right now:

- **A saved font is no longer installed.** The simulator starts with **Default (built-in)**. The Settings window explains this in a note above the list. The saved name stays in `app.ini`, so the font returns the next time it is installed. Picking a font or **Default (built-in)** replaces the saved name.
- **A saved font is installed but cannot be read.** The simulator uses the built-in font and shows a note. The saved name is kept, as above.
- **A font you pick cannot be read.** The font in use stays as it was, and the note says so. Nothing is saved.

The Linux font list is not built or tested yet. Until it is, Linux shows **Default (built-in)** only, and the choice is not saved between runs on Linux.

## Limits

- Font collections (`.ttc` files) are not listed. A family that is only available as a collection does not appear in the list.
- The font size stays the same as the built-in font's. The picker changes the typeface only.
- Numeric fields and readout strips are sized from the measured text of the current font. They have not been checked in a wide font, so a much wider font may still need a wider window.
