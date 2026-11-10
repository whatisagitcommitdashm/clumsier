# Clumsier macOS local beta

## Before installing

This experimental PF/dummynet build affects **all TCP traffic**, not just
Minecraft. UDP is not delayed. Stop and quit restore normal traffic on a
best-effort basis; a helper crash/power loss may need the scoped recovery
commands in its log. Never flush the entire firewall or all pipes.

This beta package is **not notarized** and is not yet clean-machine validated.
An Apple Development signature is not a Developer ID public release signature.
Do not disable SIP or Gatekeeper, or run blanket quarantine-removal commands.
The downloaded artifact must pass a separate Gatekeeper acceptance check before
it is described as a smooth public installer. Do not publish it as stable.

Use only an artifact from the maintainer/fork release you trust. Verify its
published SHA-256 checksum. The package's embedded package-audit.json states
the actual minimum macOS version. This build is native-architecture only.

## Install and play

1. Quit any running Clumsier, then wait 70 seconds for helper cleanup.
2. Open the .pkg and follow Installer. It places Clumsier and Uninstall Clumsier
   in Applications, and its private runtime in /Library/Application Support/Clumsier.
   No Python, Qt, Homebrew or repository checkout is needed by the installed app.
3. Open Clumsier from Applications. In setup choose **Authorize networking**.
   A macOS administrator prompt starts only the scoped networking helper; the
   GUI never runs as root. Authorization does not enable delay or create a
   login item/service. You may need approval again next session.
4. Optional: on Hotkeys enable Input Monitoring in System Settings and return.
   Record a keyboard key to verify keyboard access, not just mouse capture.
   macOS may require quitting/reopening. Leave this off to use playback buttons.
5. Choose a small delay and direction, then Start. No server selection needed.
   Start with a practice run; other TCP applications can slow down too.

Use **Set up networking** again if the helper expires. Stop removes conditioning;
quit/disconnect clears it, then the helper exits after 60 seconds without a GUI.
A connected unresponsive GUI expires after 10 seconds without a heartbeat.
Global keys deliberately pass through; Cmd+Q while Minecraft is focused can quit
the game. Close the focused Clumsier window to test Clumsier shutdown safely.

## Update and uninstall

Quit Clumsier and wait 70 seconds before running a newer package. Updates retain
presets; keep the same publisher signature. Changed signing identity can require
fresh Input Monitoring approval. Open **Uninstall Clumsier** in Applications for
administrator-authorized removal. It refuses removal while the helper is active
or unresolved sockets remain. It preserves presets, privacy choices and recovery
logs. It never flushes network rules. Remove the Uninstall Clumsier app afterward.

Logs: /var/run/clumsier-lag-USER_ID.log (administrator read access). If cleanup
fails, obtain help using that session's exact anchor/pipe/reference commands.
Do not delete a live socket to force a second helper.

## Release acceptance still required

Test the actual downloaded .pkg on a clean Mac: Gatekeeper, install/update,
root ownership, authorization cancel/retry, start/stop, close/reopen, forced GUI
exit, uninstall, keyboard permission grant/deny/revoke, game-focused hotkeys,
HUD, all-TCP side effects, IPv6/VPN/sleep if supported. Do not count build-tree
success as clean-machine installation evidence.

## Build the beta (maintainers only)

Use a dedicated Python 3.12 environment with PyInstaller 6.22.3 and the Qt SDK
used by the application. PyInstaller is a build dependency, not a player install
requirement. Review all bundled library licenses/source obligations before a
public release. The package pipeline audits runtime dependencies and computes
the actual minimum macOS version rather than advertising macOS 13 unconditionally.

```sh
SIGN_IDENTITY='YOUR_CERTIFICATE_FINGERPRINT_OR_NAME' \
PYINSTALLER_PYTHON='/absolute/path/to/build-venv/bin/python' \
CLUMSIER_QT_ROOT='/opt/homebrew' \
sh scripts/package-macos-local.sh
```

The output is in dist/ with a SHA-256 file. Building does not install the package,
start the helper, request administrator permission, or publish a GitHub release.
The current package is unsigned at the Installer-container level; the application
and helper binaries use the selected identity. Before general public release,
use appropriate Developer ID signing/notarization and test the downloaded file.
Do not distribute signing keys or use them in source control.

## Current local validation

The first Apple Silicon artifact is about 48 MiB and reports macOS 15.7.0 as
its minimum. Its staged application passes strict nested-signature verification
and `--check-load` with a minimal environment; the frozen helper runs `--help`
without the build Python. The dependency audit inspected 167 Mach-O files and
rejected external absolute dependencies and escaping symlinks. This does not
prove operation on a machine without the development tools installed.

31 Python tests and the six macOS-specific Qt tests pass, including a simulated
first-run setup dialog (no authorization prompt or networking changes). The full
Qt suite is not green: `qt-shell` and `qt-bridge` fail keyboard/window-activation
checks in this session. They remain release blockers pending interactive rerun
and investigation, not tests to disable. No privileged installation, update,
uninstall or actual authorization dialog has been exercised by these checks.

This artifact is for local acceptance testing, not a published player release.
Before publishing, complete the above manual matrix, downloaded-file security
checks, and artifact-specific third-party license/source review. The bundled
notices collector currently targets the tested Qt 6.11.2/Python 3.12.8 build;
update it when changing SDK/runtime versions.

## App ZIP beta 2

The app-only download is `Clumsier-macOS-arm64-beta.2.zip`. Unzip, move
Clumsier.app to Applications, and launch it there. In setup click **Install
networking components…**, finish Apple Installer, then **Authorize networking**.
The embedded helper-only package deliberately permits the GUI to remain open,
but refuses replacement while a network helper or unresolved endpoint is active.
It installs the same protected root-owned runtime as the full package. The ZIP
is not a portable app that runs its privileged runtime from a writable bundle.
Both the downloaded app and embedded installer may require per-item approval in
Privacy & Security. This new install path still needs independent manual testing.

To reproduce: run `scripts/package-macos-local.sh` with the selected identity
and build Python, then `SIGN_IDENTITY=... sh scripts/package-macos-app.sh`.
The second script consumes the first script's staged payload; do not use stale
staging from an older source revision. It never installs or authorizes anything.

Website source is in `website/`; deploy with `vercel deploy website --prod`.
It uses no framework, analytics or external fonts. Update its versioned download
link alongside each release. Axe artwork attribution is in
`resources/macos/ATTRIBUTION.txt` and `website/assets/ATTRIBUTION.txt`.

## DMG download

The website now links to `Clumsier-macOS-arm64-beta.2.dmg`. Open the disk image,
drag Clumsier.app onto Applications, then open it from Applications. Eject the
disk image afterward. Networking installation and privacy approval stay the same.
The DMG does not remove Gatekeeper checks and is not notarized.

Maintainers can run `sh scripts/package-macos-dmg.sh` after making the app ZIP.
It verifies the app signature before packaging and verifies the resulting image.
The original ZIP stays available as an alternative release asset.

The DMG packager also authors Finder's saved icon-view layout: Clumsier on the
left, Applications on the right, a background arrow and short instructions.
Builds need an interactive macOS Finder session and may request Finder automation
permission. Layout metadata is saved on a temporary writable image before final
read-only compression. The guide text is retained inside the hidden background
folder; the website remains the visible setup guide.

## Intel testing beta (macOS 12+)

The Intel download is separate from the Apple Silicon build. Build with Qt 6.8.3's
universal macOS SDK and an Intel-capable Python with PyInstaller 6.22.3 installed.
Rosetta is required when packaging on an Apple Silicon host. Run:

```sh
SIGN_IDENTITY=YOUR_CERTIFICATE \
CLUMSIER_QT_ROOT=/path/to/Qt/6.8.3/macos \
PYINSTALLER_PYTHON=/path/to/intel-venv/bin/python \
sh scripts/package-macos-intel.sh

CLUMSIER_DMG_ARCH=x86_64 CLUMSIER_DMG_MINIMUM=12.0 CLUMSIER_DMG_BETA=3 \
sh scripts/package-macos-dmg.sh dist/Clumsier-macOS-x86_64-beta.3.zip \
  dist/Clumsier-macOS-x86_64-beta.3.dmg
```

The default build SDK is the locally available macOS 15.5 Command Line Tools
SDK. Override `CLUMSIER_MACOS_SDK` with another compatible SDK. Xcode 26 removed
AGL, which Qt 6.8 links on Intel, so its SDK cannot build this kit unmodified.
The packaging audit rejects every missing Intel slice, external non-system
library dependency, and binary requiring newer than macOS 12. The optional
Network Extension backend requires macOS 13; PF/dummynet is the default on 12.

Build and all six macOS-specific Qt tests passed under Rosetta on macOS 15.7.
The packaged GUI and helper help command loaded under Rosetta without developer
PATH entries. No real Intel or macOS 12 installation/gameplay test has run yet.

Before calling Intel supported, a player needs to test: downloading and dragging
into Applications; per-item Gatekeeper approval; installing/authorizing the helper;
0 ms Start, outbound delay, Stop; Input Monitoring and keyboard recording; HUD
in windowed/full-screen Minecraft; quit/reopen and traffic recovery; uninstall.
Use a practice run first. Report the exact macOS version and Mac model, no serial.

If replacing the old full-PKG installation fails with a permissions warning,
quit Clumsier, wait 70 seconds, move only Applications/Clumsier.app to Trash
(authenticate if asked), then drag the new app from the DMG. Do not remove the
root-owned networking helper or change its permissions.
