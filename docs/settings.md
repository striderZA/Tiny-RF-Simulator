# Settings

The **Settings** menu sits between **Tools** and **Help** in the menu bar. It holds the simulator's preferences — the interface font, its style and size — and the saved-window-layout actions.

## Manage saved layouts

The lower part of the **Settings** menu holds the saved window-layout actions:

- **Settings > Save As...** saves the current arrangement — the main window and the Utility Space — under a name.
- **Settings > Manage...** opens the Manage Layouts window, where saved layouts can be loaded, renamed, or deleted.

**View > Layouts > Load** stays in the View menu for switching between saved layouts; pick a name there to restore it.

## Choose a UI font

1. Choose **Settings > UI Font...** to open the Settings window.
2. Pick a family from the **UI font** list. The list holds **Default (built-in)** and every monospaced font the system has installed, sorted by name. Each family appears once, however many styles it has.
3. Choose a **Style** for the family. The list offers only the styles that family has, such as Regular, Light, Italic, or Bold Italic. Each style appears once. Where a family names several faces alike, such as Regular at several weights, each one is labelled with its weight, for example Regular (100). Weights that come from one variable font file draw the same face, so they appear as one row. The style sets the body text. Picking a family starts at Regular.
4. Under **Bold text uses**, choose **Bold**. Choose **Medium** where the family has a Medium style; the option does not appear otherwise.
5. Set the **Font size** slider, from 10 to 24 px.
6. Check the **Preview** section. It shows the name of the font in use, a sample line in the body text, and a sample line in bold text.

The font applies to the whole interface as soon as you pick it. Pick **Default (built-in)** to return to the font, style, emphasis, and size the simulator ships with.

Only monospaced fonts are listed, so the numbers in tables and readouts line up in columns. Each family appears once, even when the system reports several styles of it.

## Bold text and size

- The section headings in the Settings window use the bold text face, so you can see the choice there.
- A family without a Bold style uses its Regular style for bold text. The Settings window says so in a note above the list.
- The font size applies to the whole interface, including **Default (built-in)**. It is measured in pixels at 100% scale.
- While you drag the **Font size** slider, the interface keeps its current size. It updates when you release the slider, or once the value has stopped changing for a moment while the slider is still held.

## What is saved

The choice is saved with the simulator's settings, in `app.ini` beside the executable on Windows, so the next start uses the same font, style, bold text choice, and size. Your choice is kept even when it cannot be used right now:

- **A saved font is no longer installed.** The simulator starts with **Default (built-in)**. The Settings window explains this in a note above the list. The saved name stays in `app.ini`, so the font returns the next time it is installed. Picking a font or **Default (built-in)** replaces the saved name.
- **A saved font is installed but cannot be read.** The simulator uses the built-in font and shows a note. The saved name is kept, as above.
- **A font you pick cannot be read.** The font in use stays as it was, and the note says so. Nothing is saved.
- **A saved style is no longer available in its family.** The simulator uses the nearest style the family has, and the note names both styles. The saved style stays in `app.ini`.
- **A saved size is outside 10 to 24 px.** The simulator uses the nearest limit, and the note says so.
- **A preference saved by an earlier version.** A saved family with no style or size loads as that family's Regular and Bold styles at the built-in size.

On Linux the list is built through fontconfig. That path has not been run here, so treat the list as unverified there, and the choice is not saved between runs on Linux (the settings are stored in `app.ini` on Windows only).

## Limits

- Font collections (`.ttc` files) are not listed. A family that is only available as a collection does not appear in the list.
- The size is in pixels at 100% scale. The simulator does not change it for your display's scaling.
- Numeric fields and readout strips are sized from the measured text of the current font. They have not been checked in a wide font, so a much wider font may still need a wider window.
