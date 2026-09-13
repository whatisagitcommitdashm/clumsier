# Clumsier

A network-delay application evolving from Clumsy, with a Qt Quick interface, configurable hotkeys, shareable sequences, and an in-game HUD.

**Development status.** The Qt beta is the main application. Windows has live
Lag controls, saved sequences, global hotkeys, and a HUD; Linux has a connected
Qt frontend and native backend, with platform work still pending. macOS remains
a separate prototype. The IUP application is retired. Beta naming does not mean
the security review or release acceptance checks are complete.

To run a published Windows beta, extract the Windows x64 ZIP from
[Releases](https://github.com/whatisagitcommitdashm/clumsier/releases) and open
`clumsier-beta.exe`. Keep the `app` folder beside it; see `app/START-HERE.txt`
for runtime installation and first-launch setup.

To build it yourself without typing commands, extract the source ZIP and
double-click **Build Clumsier.cmd**. See [Building Clumsier](docs/BUILDING.md)
for prerequisites and [Releasing](docs/RELEASING.md) for package provenance.

## Origins and attribution

Independently maintained by [whatisagitcommitdashm](https://github.com/whatisagitcommitdashm), starting from [Auzgame/clumsy0.3-with-keybinds](https://github.com/Auzgame/clumsy0.3-with-keybinds), itself derived from [jagt/clumsy](https://github.com/jagt/clumsy) by Chen Tao and contributors. Original Git history and license notices are preserved. This is not an official release from either upstream project.

Baseline commit: `357e1e67a5803ffdd7d483b463575508a0a5f578`.

## Qt application

The [Qt Quick frontend](docs/QT-BETA.md) now connects the new interface
to saved sequences, server profiles, global hotkeys, and the Windows Lag backend.
This integration build is ready for manual testing. Its design draws
inspiration from [Monkeytype](https://monkeytype.com), Zen Browser's compact
sidebar, and Ninjabrain Bot's compact in-game display.

## Linux prototype

The [Qt frontend is now connected to Linux](docs/QT-LINUX.md). Open it with
`sh scripts/run-qt-linux.sh`, or use `sh scripts/demo-qt-linux.sh` for an isolated
GUI demo with live ping measurements. Linux global hotkeys remain pending.

## Global hotkeys

The defaults are **F5: Start**, **F6: Stop**, and **F7: Toggle**. Start leaves an already running capture running; Stop leaves a stopped capture stopped. Toggle switches between those states.

Click a binding field or its **Record** button. Hold the desired keys and/or mouse buttons together, then release them. The first release fixes the combination; recording finishes once all inputs are released. The opening click is excluded. Click **Cancel** to discard a recording; Escape itself can be recorded. Normal hotkey actions are suspended during recording.

Validated bindings are saved immediately. Each binding and the global listener have an **Enabled/Disabled** button; mouse controls remain available when hotkeys are disabled. Next step, Previous step, and Reset sequence are initially unassigned. Search and New sequence are customizable window shortcuts, defaulting to Ctrl+F and Ctrl+T.

Supported combinations include bare letters or digits (`W`, `7`), several ordinary keys (`Q + E`), modifiers, and mouse buttons (`Ctrl + Mouse4`). Mouse1/2/3 mean left/right/middle; Mouse4/5 are the two extra buttons. Left and right modifiers are interchangeable. Function keys, navigation keys, punctuation, and other Windows virtual keys are also supported. Uncommon keys use a stable `KeyXX` label; punctuation labels follow US key names. Keyboard hardware and Windows shortcuts can limit which combinations are observable. Mouse-wheel bindings and sequences of separate presses are not implemented yet.

**Input passes through:** Clumsier forwards keyboard and mouse presses and releases, so other applications can also act on the same combination. This replaces the previous exclusive Windows hotkey registration, so Clumsier no longer reports another application's registered shortcuts as conflicts.

A combination fires when all its inputs are down, regardless of their press order. Extra held inputs are allowed, so movement keys do not block another hotkey. Holding a combination does not repeat it; releasing and pressing one of its members again rearms it. Two bindings cannot be identical or contain one another (for example, `Q` and `Q + E`). Otherwise both matching bindings may fire if their inputs are held together. Hotkeys are global, including while Clumsier is focused; recording and preset/profile dialogs are exceptions.

Settings remain in `%LOCALAPPDATA%\Clumsier\hotkeys.ini`. Existing version-1 and version-2 settings load automatically with the sequence actions unassigned; Saving a binding writes version 3. Failed validation or saving leaves the active bindings unchanged. Invalid files are reported and preserved until you explicitly save replacement settings. Bindings remain separate from packet-filter presets.

## Presets and sequences

The app opens in simple mode on **Quick controls**, where you can turn effects on and off without a sequence. **Sequences** and **Hotkeys** are the other two tabs. Toggle **Advanced mode** to show manual effects and network settings. New sequences use inbound traffic with no address or port restriction; editing an existing sequence preserves its settings in either view.

Use the **Sequences** tab to create, edit, duplicate, import, and export Lag sequences. Each preset shares one traffic selection across its named steps. Choose added delays directly, or specify target pings and select a local baseline profile for your server. **Lowest available** means no added delay, independent of baseline.

Selecting a sequence starts at step 1. Switching sequences or Quick Controls while running applies the selected settings and stays running; failed changes attempt to restore the previous configuration. Valid edits apply when you finish editing. Your baseline stays local; exports include the server name and original targets. Imports ask for a baseline when the server is unfamiliar.

## Planned features

- Timed sequence playback with customizable time per step.
- A searchable preset launcher.
- Optional mouse-wheel adjustment of added delay.
- A customizable home screen.
- Full support for Linux & MacOS

## Development

See [Building](docs/BUILDING.md), [Releasing](docs/RELEASING.md), and [Roadmap](docs/ROADMAP.md).

Run `scripts\test-core.cmd`, `scripts\test-lifecycle.cmd`, `scripts\test-hotkeys.cmd`, and `scripts\test-presets.cmd` to check capture behavior, hotkeys, preset calculations, persistence, and editor callbacks. These scripts use the Visual Studio C++ Build Tools. Also test shortcuts while the target game is focused, including holding a key and switching between Start, Stop, and Toggle.

## Source layout

- `src/core/`: plain C settings, preset JSON/calculations, sequence controller, action identifiers, and input matching. No Windows or IUP headers.
- `src/backends/windows/`: the WinDivert adapter, structured traffic-filter translation, and Lag queue. `legacy/` contains the inherited packet capture and packet-list machinery still used by Lag.
- `src/platform/windows/`: global input listening, Windows key names/settings files, preset/profile storage. The Qt executable requests elevation through its build manifest.
- `src/ui/qt/`: the main Qt application, sequence editor, settings, theme picker, HUD, and bridge to the shared controller.

The controller owns accepted Lag settings and queries the backend for running state. Failed live updates leave accepted settings unchanged. Backends implement start, stop, and applying Lag conditions; the shared interface does not expose WinDivert packets or handles. Settings can select traffic structurally or explicitly name a native filter backend. The existing filter editor uses the `windivert` native filter option.

**Changing the active Lag configuration immediately changes the rules governing packets that Clumsier is already holding.** Held packets retain their original enqueue times: lowering the delay can release them sooner, and raising it can hold them longer. Their waiting time does not restart when settings change. This makes live changes control the current queue as well as newly arriving packets.

Here, "immediately" means the new rules apply on the next processing pass, not that the settings call synchronously delivers packets. Disabling Lag or a direction releases the affected queued packets on processing; Stop flushes the queue. Traffic selection changes still require stopping capture first. Failed updates leave the previous rules in effect. The Windows queue retains its inherited overload limit. Other backends will need to document their scheduling precision and overload behavior.

This is an operational Windows application with a portable core, not a Linux/macOS release. Windows input IDs and filesystem access stay in the Windows adapter; preset JSON and sequence behavior live in the portable core. A future platform must supply its own key mapping and storage integration. The retained packet machinery has no IUP dependency.

## License

The original [MIT license](LICENSE) is retained; original Clumsier additions are also provided under MIT. Bundled dependencies retain their separate licenses. Release packaging must include the appropriate notices and meet their redistribution requirements.

Preset JSON uses [cJSON 1.7.19](external/cjson/README.clumsier.md), with its [MIT notice](external/cjson/LICENSE) retained.

The Qt application includes selected [Tinted Theming palettes](third_party/tinted-schemes/README.md), with their [MIT notice](third_party/tinted-schemes/LICENSE) retained. Its theme picker is inspired by Monkeytype and implemented independently.
