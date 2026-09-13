# Building Clumsier

The main application is the Qt Quick beta, with live Lag controls, sequences,
and a HUD. The IUP application and its build routes have been retired. Beta is
currently a development label; it does not establish release readiness.

## Windows

For a build without typing commands, extract the source download and
double-click **Build Clumsier.cmd** in its top-level folder. The helper checks
for the tools below, offers guidance for missing installations, and lets you
select a Qt kit folder. Run it again after installing missing tools. A console
shows build progress, but requires no commands. The result opens in File Explorer;
the build log is saved to `work/build-from-source.log`. No Git installation is
needed to build the source ZIP. Allow several GB for the development tools.

Install Visual Studio 2022 C++ Build Tools (including the Windows SDK), CMake,
and a Qt MSVC 2022 x64 kit with Qt Quick and Quick Controls. The CMake project
requires Qt 6.8 or newer; the current local kit is Qt 6.10.3.

From the repository root:

```powershell
# Set this if your kit is installed elsewhere:
$env:CLUMSIER_QT_ROOT = 'C:\Qt\6.10.3\msvc2022_64'
.\scripts\build-qt-beta.cmd
.\scripts\test-qt-beta.cmd
```

Without that override, the script uses `bin/Qt/6.10.3/msvc2022_64`.
The script selects the x64 compiler, builds with CMake, and deploys to
`bin/qt-beta/clumsier-beta.exe`. Close that executable before rebuilding it.
Launching requests administrator permission; capture begins only after Start.
Your existing Qt preferences, hotkeys, and sequence library remain in place.
Old binaries in ignored output folders are not removed by this source cleanup.

For interface behavior and packaging instructions, see [Qt beta](QT-BETA.md).
See [Releasing](RELEASING.md) for clean packaging, source downloads, dependency
notices, and the final manual checks. Test executables are not shipped in the
runnable ZIP; their source remains in the source ZIP.

## Checks

```powershell
.\scripts\test-core.cmd
.\scripts\test-lifecycle.cmd
.\scripts\test-hotkeys.cmd
.\scripts\test-presets.cmd
.\scripts\test-qt-beta.cmd
```

These cover shared logic, simulated packet lifecycle, Windows hotkeys, preset
parsing/storage, and Qt behavior. The Qt tests require the Qt build first.
The retired IUP callback tests have been removed. These automated Windows checks
do not establish real game timing; manually check delay changes, sequence
switching, hotkeys, HUD behavior, and Stop restoring normal traffic.

## Other platforms

[Qt on Linux](QT-LINUX.md) describes the same frontend with the Linux helper,
including build prerequisites and isolated network tests. [Linux](LINUX.md)
also documents the terminal diagnostic frontend.

[macOS](MACOS.md) describes the separate native prototype and its unverified
native build and packet-testing requirements. It is not yet the integrated Qt
application available on Windows and Linux.
