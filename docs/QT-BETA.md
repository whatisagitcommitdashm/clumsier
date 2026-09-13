# Qt application beta

The Qt Quick frontend is the main Clumsier application. On Windows it uses the
preset library, C controller, WinDivert Lag backend, and global hotkey listener.
The IUP application is retired. Only Lag is supported; beta naming is not a
claim that release validation is complete. See [Building](BUILDING.md) first.

For Linux build, launch, isolated demo, storage, and test instructions, see
[Qt on Linux](QT-LINUX.md). The Windows instructions below remain applicable to
the Windows build. Linux now has live networking and storage adapters, with
global hotkeys still pending.

## Run

Close any older Clumsier instance before opening
`bin/qt-beta/clumsier-beta.exe`. The Windows build requests administrator
permission when opened. Capture stays stopped until Start or a configured global
Start/Toggle shortcut is pressed.

Try selecting steps, editing a target, switching tabs, resizing the window, and
using the small panel button beside **sequences**. That button switches between
a pinned list and compact mode. In compact mode, hovering near the left edge
reveals the list; it withdraws after the pointer leaves, even after clicking a
control inside it. Ctrl+B opens it for keyboard use; Escape, selecting a sequence,
or clicking outside dismisses that keyboard-opened panel. The panel button pins
it open again.

Compact mode reacts along the full left edge, including the header and footer.
The panel floats 10 pixels inside the window with rounded corners and a slightly
translucent background; text stays opaque. The reveal starts without a dwell
timer, uses a short slide, and leaves a small hover corridor across the inset gap.
Reduced motion disables the slide. Pinned mode keeps the original solid layout.

Saved Windows hotkeys are loaded through the existing adapter (default toggle: F7).
The Hotkeys tab records keyboard/mouse chords and saves validated changes immediately.
Original input continues to the game. Actions pause while typing into a field,
using a dialog/theme picker, or working in the focused Hotkeys tab.
The global **Enabled/Disabled** button turns global playback shortcuts off without
affecting mouse controls or binding editing. This preference survives restarts.

Sequences and server profiles remain in `%LOCALAPPDATA%\Clumsier`.
Save/Discard controls appear while the sequence has unsaved changes. Navigating
away offers Save, Discard, or Cancel; a failed save keeps the dialog and draft
open. Closing with unsaved edits stops capture and offers the same choices.
Duplicate creates a new draft; import validates JSON and saves a separate entry.
Sequence fields remain editable while playing; valid changes apply when editing ends. Step clicks select the active step.
Saving while stopped prepares the sequence immediately for the next Start.

Settings includes **Autosave sequences** (off by default). Valid edits save
after a 500 ms pause; incomplete or invalid entries stay editable and display
the validation error. Navigation flushes a pending save before switching.
Hotkey and autosave preferences live in `preferences.ini` beside the library.

The sequence list scrolls independently. Shift-click selects a range; Ctrl-click
adds or removes individual selections. These group selections do not change the
active delay. Right-click offers Delete, Duplicate, and Export for the selection.
Context-menu copies are saved immediately and inherit local server associations.
Export asks for a folder and creates separate, uniquely named JSON files without
overwriting previous exports. Batch operations validate all selected files first;
if a later disk operation fails, the error reports how many files were completed.

Deletion confirms by default, with a **Don’t ask me again** checkbox. Settings
can restore that confirmation at any time. Deleting an active sequence stops it
and clears Start readiness; deletion never starts a different sequence.

Click a sequence title or **Rename** to edit it. The title aligns with the server
summary and highlights on hover. Clicking outside a text field, Enter, or Escape
ends editing. Settings controls show brief themed hover explanations. Dropdowns
fade quickly; Reduce motion removes these transitions. Double-clicking the header
does not maximize the app; use the window button instead.

The quick-control HUD ends with its delay values and Start/Stop button on the
same row. Sequence HUD navigation uses closer spacing. HUD controls remain
optional, and hiding them never disables mouse controls in the main window.

Quick Controls and Sequences select what Start activates. Switching between them or selecting another sequence keeps a running activity running with the new settings. Unsaved edits still require Save/Discard when autosave is off. Hotkeys and Settings do not change the active mode. An incomplete sequence cannot start; Quick Controls remembers its last accepted delay.

Quick Controls applies an inbound/outbound delay through the shared controller. Valid fields commit on focus loss or Enter. Traffic changes restart capture internally; failures attempt to restore the previous configuration and report the result.

Target ping and Lowest available are exclusive modes. Lowest available clears
and disables the number field. Switching back starts at zero rather than
restoring a hidden old target.

Each sequence remembers its selected server through the local library. Export shares the server name, not the personal baseline or local profile ID. Imports reuse a matching local server or ask for a baseline for an unfamiliar one. The fresh beta library contains Skylands, Pirate Bay, and Good Basic, which share one Mineplex baseline prompt.

During playback, the highlighted step follows Next/Previous/Reset and hotkeys.
Clicking a step also changes playback; a rejected change leaves the active step
untouched. Previous, Reset, and Next sit beside Start/Stop in the playback area.
While stopped, clicking a row selects the step to edit.

Settings offers interface size, reduced motion, compact mode, bottom hints,
switch confirmations, and Advanced mode. Advanced mode defaults to off and
reveals the sequence traffic settings when enabled.
For compatibility with existing installations, preferences retain the internal
`Clumsier/ClumsierUiPreview`
application identity (on Windows, Qt's native settings use the current user's
registry). HUD preferences use the same settings identity.

## Build

Requirements: Visual Studio 2022 C++ x64 tools, CMake 3.21+, and a Qt MSVC 2022
x64 SDK. The local SDK is Qt 6.10.3 under `bin/Qt/6.10.3/msvc2022_64`.
It includes Qt Base, Declarative (QML/Quick/Quick Controls), and Tools.
Qt Creator is not required; VS Code can remain your editor.

From the repository root:

```bat
scripts\build-qt-beta.cmd
scripts\test-qt-beta.cmd
```

Set `CLUMSIER_QT_ROOT` to the kit directory to use a different SDK installation.
For shareable Windows and source ZIPs, use the [release packager](RELEASING.md).
It builds an isolated source snapshot, includes dependencies and notices, and
keeps the development build untouched. The packaged application uses the same
local settings and sequence library when run on the same Windows account.

The build script selects the existing Visual Studio compiler, builds in Release
mode, and deploys the Qt DLLs/QML imports beside the executable. No Python runtime
is involved. The SDK and generated binaries remain ignored by Git.

For another machine, install the MSVC 2022 x64 kit with the Qt installer, or use
[aqtinstall](https://aqtinstall.readthedocs.io/) to download Qt's SDK archives:

```bat
python -m pip install aqtinstall==3.3.0
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O bin/Qt --archives qtbase qtdeclarative qttools
```

## Code organization

- `src/ui/qt/main.cpp`: native entry point and isolated application identity.
- `src/ui/qt/Main.qml`: shell, responsive workspace, sidebar and sample state.
- `Theme.qml`: shared visual values and motion/scale preferences.
- `ThemePicker.qml`: searchable live previews, keyboard navigation, and favorites.
- `ThemeCatalog.js`, `TintedThemes.js`: bundled palettes, separate from the picker.
- `QuietButton.qml`, `QuietField.qml`, `QuietChoice.qml`, `QuietCheckBox.qml`,
  `QuietDialog.qml`: shared themed controls, including popup and pressed states.
- `tests/qt_shell.cpp`: real mouse/keyboard events, resize/render checks and
  isolated settings. Set `CLUMSIER_SCREENSHOTS` to save renders during the test.

`app_bridge.cpp` translates QML drafts to the existing strict preset JSON parser
and delegates storage, validation, playback, and hotkeys to C.
`LiveWorkspace.qml` contains the functional editors. `lag_runtime.c` supplies the
Lag-only legacy scheduler table without linking IUP or other effects.
The original visual sample remains available to shell tests and non-Windows
UI development builds; Windows and Linux now have live adapters connected.

## Design review

Judge typography, spacing, hover/focus states, tab changes, and the sidebar's
reveal/hide timing. Try 100%, 115%, and 130% interface sizes, a narrow window,
keyboard-only navigation, and moving between monitors. At narrow widths the
workspace scrolls vertically, while Start/Stop stays visible.

The custom header includes minimize, maximize/restore, and close. Drag blank
header space to move the window, double-click it to maximize/restore, and drag
edges or corners to resize. Qt starts the native move/resize operations; a small
Windows frame adapter preserves native maximize mechanics and the monitor work
area while removing the separate title bar. Check snapping, dragging between
monitors and taskbar behavior manually on your desktop.
On Windows 11, the normal window requests native rounded corners. Maximized
and snapped windows retain square edges; older Windows versions retain their
normal frame. Sequence and step rows show the theme hover color only while the
pointer is over them, independently of their selected state.
Native Linux/macOS builds and their window behavior have not been tested here.

The theme button in the header opens the picker. Search or use Up/Down to
preview; mouse previews wait for a 500 ms hover, while keyboard navigation stays
immediate. Palette colors fade over 240 ms and the picker fades in over 180 ms.
Reduce motion disables those fades. Samples show four palette colors in a pill.
Click a row or press Enter to save. Escape or clicking outside restores
the saved theme. Stars save favorites without applying the palette; favorites
appear first the next time the list opens or is filtered. Theme and favorite
choices persist separately from the production app. Check both light and dark
themes, including at 130% scale.

The catalog includes 13 MIT-licensed Tinted Theming palettes alongside six
prototype palettes. Sources, authors, mapping and regeneration instructions
are in `third_party/tinted-schemes`. No themes are downloaded at runtime.

Monkeytype inspired the typography, restrained visual hierarchy and
theme picker. Zen Browser inspired compact sidebar behavior; Ninjabrain Bot
inspired the compact HUD. This prototype contains no code copied from those
projects. Qt is dynamically linked. This is a local development build, not a
release package; include the applicable Qt/third-party licenses and source-access
information when preparing a distributable release.

## HUD draft

Use the HUD button beside playback controls, or enable it in Settings. It is a
separate always-on-top, non-activating tool window: minimizing the editor does
not hide it, and clicking its controls should leave keyboard focus in the game.
Drag its Running/Stopped label to move it. The close button only hides the HUD;
closing the main application shuts down capture and closes both windows. The
HUD only cancels standalone close requests, so it cannot veto application exit.
Process-level tests cover shutdown with real hotkey listeners, isolated settings,
and fake capture, with the HUD both shown and hidden.

The HUD shows accepted sequence/step state, estimated target ping (not a live
measurement), or added inbound/outbound delay. Stopped values are explicitly
labelled as applying when started. Its controls use the same controller and
validation as the editor. Settings offers independent size, background opacity,
and optional playback controls; text remains opaque for readability. These
preferences survive restarts; placement currently lasts for the session. Bring
HUD here recovers it beside the editor.

Check it over your actual game, including your usual display mode. A Windows event listener restores the HUD above the foreground window after
activation or fullscreen resize events, without changing focus. It is inactive
while the HUD is hidden. The native flags and editor minimization are tested;
a fullscreen foreground test also runs when Windows grants the test window
focus. True exclusive fullscreen bypasses desktop composition and is not
supported by this desktop-window approach; use borderless/windowed mode when
necessary. Actual Minecraft fullscreen behavior still needs manual verification.

Quick Controls accepts whole numbers from 0 to 15000 ms. Changes apply when editing ends, including during capture. Empty fields become zero on exit; invalid entries restore the previous value. Traffic changes are supported during capture through an internal restart.

## Sequence editor

The title is editable while stopped or running. The actions button opens description,
duplicate, delete, import, and export; secondary actions do not crowd the steps.
The server summary shows the selected name and baseline. Manage servers opens
selection and profile controls, stopping playback first when necessary. Server
choices are saved immediately; editing a shared server baseline affects every
sequence using that profile.

Step rows align numbers, names, and values. The adjacent editor shows the target
and estimated added delay, or explains why the baseline is the lowest achievable
ping. Add, Remove, and reorder stay with the list; Wrap at end stays below it.
At narrow widths the editor and playback controls stack instead of clipping.

## Integration acceptance checks

1. Start with capture stopped. Open an existing sequence, edit and save a step,
   restart, and confirm the edit and server profiles survived.
2. Try Discard, duplicate, delete (with confirmation), import, and export. Check
   both target-ping and added-delay sequences, including their traffic settings.
3. Pick a server and select a saved sequence. Start capture, advance/reset/rewind,
   and repeatedly press your Start/Stop/Toggle bindings. Lowest available should
   remove added delay promptly (server ping displays can still average samples).
4. During playback, edit a delay or target slowly. The old value should remain
   active until editing ends, then the accepted value should apply. Invalid
   entries restore the previous value. Switch between Quick Controls and
   sequences and verify capture stays running with the selected settings.
   Select no sequence (or a target sequence without a server) and verify Start
   and global Start/Toggle cannot activate a previous delay.
5. Test Quick Controls, including zero delay and Stop. Close during capture and
   confirm traffic recovers. Opening the Windows executable should request
   administrator permission before showing the app.
6. Record a letter/mouse chord, try a conflicting binding, cancel a recording,
   restart, and confirm saved bindings. Confirm input still reaches the game and
   typing in editors or the theme picker does not trigger capture actions.

7. Assign different servers to two sequences, restart, and check each restores
   its own baseline. Check the highlight follows hotkeys and clicking a step
   changes the live delay.
8. Change a title, navigate away, and test Save, Discard, and Cancel. An invalid
   title must keep you in the editor after a failed Save. Try closing with edits.
9. In Settings, enable Advanced mode and restore deletion confirmations. Restart
   and verify both preferences. Review dropdowns and checkbox presses in light
   and dark themes.

Automated bridge tests use a temporary library and fake network backend, with no
global hooks. They cover disk persistence, import/export, draft protection,
validation, active snapshots, failed applies/starts, idempotent actions, and live
QML loading. The real WinDivert driver and in-game input still need manual checks.
