# Qt interface on Linux

The Qt frontend now uses the Linux NFQUEUE backend for Quick Controls and
sequence playback. Presets, baseline profiles, server associations, import/export,
and interface preferences persist in your normal user account. This is still a
local integration build; only Lag is connected.

## Open the app

From the repository root:

```sh
sh scripts/run-qt-linux.sh
```

In **Quick controls**, enter an added delay, choose a direction while stopped,
and press **Start**. The first Start opens your desktop's administrator
confirmation for the small `clumsier-linux-helper` executable. Approve it to allow
packet capture. The GUI itself stays unprivileged; opening the app or editing a
sequence needs no administrator access. Stop releases held traffic and removes
capture rules. Closing the GUI closes the helper's input pipe and cleans up.
The helper stays available while the app is open, so subsequent Start/Stop cycles
do not require another prompt.

Use **Sequences → actions → Import** to import `examples/four-leaps.json`.
Select/create a server profile with baseline 50 ms, then Start and advance through
the steps. That example adds 150, 0, 100, and 50 ms inbound. For real use, select a
baseline appropriate to your server. Quick controls initially select all IP
traffic in the chosen direction; use a sequence's Advanced traffic settings to
restrict an address, protocol, or remote port.

Global hotkeys are not implemented on Linux yet. The Hotkeys page says so, and
the Start button does not advertise the Windows F7 shortcut. On-screen playback
controls work. Wayland window placement, always-on-top HUD behavior, and behavior
over a particular game still depend on the compositor and need manual review.

## Try it without affecting your connection

```sh
sh scripts/demo-qt-linux.sh
```

This opens the same Qt interface inside the isolated two-namespace network lab
and continuously prints `ping -U` results in the terminal. Set 150 ms inbound in
Quick controls, press Start, try zero or another value, then Stop. The displayed
RTT should follow your changes. Close the GUI to clean up the lab.

This mode requires no administrator prompt and has no external network route.
Its library and preferences are temporary. Use the normal launcher above for
persistent settings and real-network use. Plain ping can hide inbound delay due
to kernel receive timestamps; the demo uses delivery-to-userspace timing.

## Build

The tested setup uses GCC 15, CMake 3.31, Qt 6.10.3, and libnetfilter_queue 1.0.5
on Fedora 42. For a system with compatible Qt development packages:

```sh
sudo dnf install cmake gcc-c++ libglvnd-devel libnetfilter_queue-devel qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland
sh scripts/build-qt-linux.sh
```

On this development machine, installed Hyprland packages require Qt 6.9's private
API while Fedora's available development packages require Qt 6.10. We therefore
use an SDK in the ignored `build/Qt` directory, leaving the desktop's Qt untouched.
CMake automatically selects `build/Qt/6.10.3/gcc_64` when present. To reproduce:

```sh
sudo dnf install cmake gcc-c++ libglvnd-devel libnetfilter_queue-devel
python3 -m venv build/qt-tools
build/qt-tools/bin/pip install aqtinstall==3.3.0
build/qt-tools/bin/aqt install-qt linux desktop 6.10.3 linux_gcc_64 -O build/Qt --archives qtbase qtdeclarative qttools qtwayland icu
sh scripts/build-qt-linux.sh
```

[aqtinstall's CLI documentation](https://aqtinstall.readthedocs.io/en/v3.3.0/cli.html)
describes the SDK download options. Set `CLUMSIER_QT_ROOT` to another compatible
Qt 6.8+ SDK to use it instead. The launcher clears inherited Qt plugin/theme
search overrides so desktop plugins compiled against another Qt version are not
loaded into this app. Do not move only the executable away from its build/SDK
paths; packaging and installation rules remain future work.

## Storage and privileges

- Library: `$XDG_DATA_HOME/Clumsier`, normally `~/.local/share/Clumsier`.
- Presets and profiles: `presets/*.json` and `profiles/*.json`, with generated UUID
  filenames. Profiles use the existing version-1 format.
- Server associations: `sequence-servers.ini` inside the library.
- Qt preferences: normally `~/.config/Clumsier/ClumsierUiPreview.conf`.

Writes use Qt's atomic-save mechanism, with no direct-overwrite fallback.
Malformed library files are reported/skipped and preserved. The helper does not
read or write the library. It accepts only bounded start/apply/stop/status JSON
messages over private stdin/stdout pipes and uses the same validated Linux
backend as the terminal prototype. Backend errors are surfaced in the UI.

Normal capture requires `pkexec` (polkit) and a desktop authentication agent.
Cancelling/denying authorization keeps capture stopped and reports an error;
Start can be retried. `--direct-helper` is a development option for an already
privileged or isolated test environment; the demo sets up that environment.
The desktop password-dialog interaction still needs manual acceptance on the
user's session; automated packet tests use namespace-scoped privileges.

## Verification

```sh
sh scripts/test-qt-linux.sh
sh scripts/test-linux-network.sh --qt
```

The first command runs offscreen Qt shell/editor tests with a fake backend plus
Linux storage tests: persistence, imports/exports, draft protection, profile
associations, malformed profiles, Unicode paths, and failed atomic saves.

The second uses real Qt button events, the bridge, pipe transport, helper, and
NFQUEUE in isolated namespaces. It verifies measured delay, sequence steps,
zero delay, held-packet Stop, bridge destruction, malformed helper requests,
and cleanup after GUI pipe loss. It also verifies unrelated nftables tables
remain intact. Neither command changes the host's network rules or user's library.
