# Qt interface prototype

The Qt Quick frontend now uses the existing Windows preset library, C controller,
WinDivert Lag backend, and global hotkey listener. It remains a separate executable
from the IUP application. This is an integration build for manual testing, not a
finished release; only Lag is wired into this frontend.

## Run

Close the old IUP application before using this build so both applications do not
listen to the same hotkeys or capture the same traffic. Open
`bin/qt-preview/clumsier-ui-preview.exe`. The Windows build requests administrator
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

Sequences and server profiles share `%LOCALAPPDATA%\Clumsier` with the IUP app.
Save/Discard controls appear while the sequence has unsaved changes. Navigating
away offers Save, Discard, or Cancel; a failed save keeps the dialog and draft
open. Closing with unsaved edits stops capture and offers the same choices.
Duplicate creates a new draft; import validates JSON and saves a separate entry.
While playing, the sequence fields are read-only and step clicks remain live.
Choose Edit to stop through the existing confirmation before changing anything.
Saving while stopped prepares the sequence immediately for the next Start.

Quick Controls and Sequences select what Start activates. Switching between them,
or selecting another sequence, stops capture. A confirmation appears before
switching a running activity; Cancel keeps the current page and capture.
**Don’t show this message again** persists across restarts and only suppresses
the warning: switching still stops capture. You can restore the confirmation in
Settings. Hotkeys and Settings do not change
the active mode. A missing or incomplete sequence disables Start, including
global Start/Toggle actions. Returning to Quick Controls restores its last
applied delay and direction for this session.

Quick Controls applies an inbound/outbound delay through the same controller.
Changing traffic selection requires Stop; failed operations leave accepted state
unchanged. Start/Stop and next/previous/reset operate on accepted controller state.

Target ping and Lowest available are exclusive modes. Lowest available clears
and disables the number field. Switching back starts at zero rather than
restoring a hidden old target.

Each sequence remembers its selected server in the local `sequence-servers.ini`
file alongside the preset library. Existing and imported sequences need a server
selected once. This association is not exported: other players select their own
server baseline. Duplicates initially inherit the original server.

During playback, the highlighted step follows Next/Previous/Reset and hotkeys.
Clicking a step also changes playback; a rejected change leaves the active step
untouched. Previous, Reset, and Next sit beside Start/Stop in the playback area.
While stopped, clicking a row selects the step to edit.

Settings offers interface size, reduced motion, compact mode, bottom hints,
switch confirmations, and Advanced mode. Advanced mode defaults to off and
reveals the sequence traffic settings when enabled.
Those preferences persist under the separate `Clumsier/ClumsierUiPreview`
application identity (on Windows, Qt's native settings use the current user's
registry). HUD preferences use the same settings identity.

## Build

Requirements: Visual Studio 2022 C++ x64 tools, CMake 3.21+, and a Qt MSVC 2022
x64 SDK. The local SDK is Qt 6.10.3 under `bin/Qt/6.10.3/msvc2022_64`.
It includes Qt Base, Declarative (QML/Quick/Quick Controls), and Tools.
Qt Creator is not required; VS Code can remain your editor.

From the repository root:

```bat
scripts\build-qt-preview.cmd
scripts\test-qt-preview.cmd
```

Set `CLUMSIER_QT_ROOT` to the kit directory to use a different SDK installation.
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
UI development builds; only Windows currently has live adapters connected.

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

Quick Controls applies each valid whole number (0–15000 ms) as you type, including
during capture. There is no Apply button. Invalid or empty input stays editable
with an inline explanation; the last valid value remains selected. Test typing
slowly across status updates, correcting invalid text, and entering zero while
running. Traffic direction changes apply while stopped and are locked during capture.

## Sequence editor

The title itself is editable while stopped. The actions button opens description,
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
4. During playback, fields should be read-only and clicking steps should change
   playback. Choose Edit: Cancel should keep playback running; Continue should
   stop before unlocking the fields. Switch from Quick Controls to Sequences and between
   sequences: Cancel should keep capture running, Continue should stop it and
   select the new activity. Repeat after restarting with the warning disabled.
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
9. In Settings, enable Advanced mode and restore switch confirmations. Restart
   and verify both preferences. Review dropdowns and checkbox presses in light
   and dark themes.

Automated bridge tests use a temporary library and fake network backend, with no
global hooks. They cover disk persistence, import/export, draft protection,
validation, active snapshots, failed applies/starts, idempotent actions, and live
QML loading. The real WinDivert driver and in-game input still need manual checks.
