# Qt interface prototype

This is the first UI migration checkpoint: a native C++ executable hosting Qt
Quick/QML. It is separate from the working Windows application and has no
WinDivert connection. Start/Stop and the sample sequences are simulated.

## Run

Open `bin/qt-preview/clumsier-ui-preview.exe`. No administrator rights are needed.

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

F7 starts/stops the preview; F8 advances its sample active step. These shortcuts
only work while the preview has focus. The real app's saved global shortcuts are
not loaded or modified. Sample edits are discarded on exit.

Target ping and Lowest available are exclusive modes. Lowest available clears
and disables the number field. Switching back starts at zero rather than
restoring a hidden old target.

Settings offers interface size, reduced motion, compact mode, and bottom hints.
Those preferences persist under the separate `Clumsier/ClumsierUiPreview`
application identity (on Windows, Qt's native settings use the current user's
registry). The theme picker, HUD, preset file operations, and real hotkey editor
are later checkpoints. Buttons for those unfinished preset operations explain
their status instead of pretending to save files.

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
- `QuietButton.qml`, `QuietField.qml`: reusable themed controls with focus states.
- `tests/qt_shell.cpp`: real mouse/keyboard events, resize/render checks and
  isolated settings. Set `CLUMSIER_SCREENSHOTS` to save renders during the test.

The sample state in QML is temporary. The next integration stage will replace it
with accepted state from the C controller through a C++ bridge. Network processing
will not move into QML.

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
inspired the planned HUD. This prototype contains no code copied from those
projects. Qt is dynamically linked. This is a local development build, not a
release package; include the applicable Qt/third-party licenses and source-access
information when preparing a distributable release.
