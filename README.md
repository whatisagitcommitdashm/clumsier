# Clumsier

A Windows network-condition tool evolving from Clumsy, with configurable hotkeys, shareable delay sequences, and plans for a cleaner interface.

**Development status.** Start and Stop have been stabilized and tested. Configurable global hotkeys, saved Lag sequences, and local baseline profiles are implemented. The preset interface is ready for manual testing; the broader interface redesign is still ahead.

## Origins and attribution

Independently maintained by [niko (whatisagitcommitdashm)](https://github.com/whatisagitcommitdashm), starting from [Auzgame/clumsy0.3-with-keybinds](https://github.com/Auzgame/clumsy0.3-with-keybinds), itself derived from [jagt/clumsy](https://github.com/jagt/clumsy) by Chen Tao and contributors. Original Git history and license notices are preserved. This is not an official release from either upstream project.

Baseline commit: `357e1e67a5803ffdd7d483b463575508a0a5f578`.

## New interface preview

A separate [Qt Quick frontend](docs/QT-UI-PREVIEW.md) now connects the new interface
to saved sequences, server profiles, global hotkeys, and the Windows Lag backend.
This integration build is ready for manual testing. Its design draws
inspiration from [Monkeytype](https://monkeytype.com), Zen Browser's compact
sidebar, and Ninjabrain Bot's compact in-game display.

## Global hotkeys

The defaults are **F5: Start**, **F6: Stop**, and **F7: Toggle**. Start leaves an already running capture running; Stop leaves a stopped capture stopped. Toggle switches between those states.

Click a binding field or its **Record** button. Hold the desired keys and/or mouse buttons together, then release them. The first release fixes the combination; recording finishes once all inputs are released. The opening click is excluded. Click **Cancel** to discard a recording; Escape itself can be recorded. Normal hotkey actions are suspended during recording.

Choose **Apply & Save** to activate the edited bindings and remember them across restarts. **Clear** unassigns a binding in the editor. **Show defaults** fills in F5/F6/F7 and clears the sequence bindings; both changes also require Apply & Save. Next step, Previous step, and Reset sequence are initially unassigned; record bindings for them in the Hotkeys tab.

Supported combinations include bare letters or digits (`W`, `7`), several ordinary keys (`Q + E`), modifiers, and mouse buttons (`Ctrl + Mouse4`). Mouse1/2/3 mean left/right/middle; Mouse4/5 are the two extra buttons. Left and right modifiers are interchangeable. Function keys, navigation keys, punctuation, and other Windows virtual keys are also supported. Uncommon keys use a stable `KeyXX` label; punctuation labels follow US key names. Keyboard hardware and Windows shortcuts can limit which combinations are observable. Mouse-wheel bindings and sequences of separate presses are not implemented yet.

**Input passes through:** Clumsier forwards keyboard and mouse presses and releases, so binding W does not block movement in the game. Other applications can also act on the same combination. This replaces the previous exclusive Windows hotkey registration, so Clumsier no longer reports another application's registered shortcuts as conflicts.

A combination fires when all its inputs are down, regardless of their press order. Extra held inputs are allowed, so movement keys do not block another hotkey. Holding a combination does not repeat it; releasing and pressing one of its members again rearms it. Two bindings cannot be identical or contain one another (for example, `Q` and `Q + E`). Otherwise both matching bindings may fire if their inputs are held together. Hotkeys are global, including while Clumsier is focused; recording and preset/profile dialogs are exceptions.

Settings remain in `%LOCALAPPDATA%\Clumsier\hotkeys.ini`. Existing version-1 and version-2 settings load automatically with the sequence actions unassigned; Apply & Save writes version 3. Failed validation or saving leaves the active bindings unchanged. Invalid files are reported and preserved until you explicitly save replacement settings. Bindings remain separate from packet-filter presets.

## Presets and sequences

The app opens in simple mode on **Quick controls**, where you can turn effects on and off without a sequence. **Sequences** and **Hotkeys** are the other two tabs. Toggle **Advanced mode** to show manual effects and network settings. New sequences use inbound traffic with no address or port restriction; editing an existing sequence preserves its settings in either view.

Use the **Sequences** tab to create, edit, duplicate, import, and export Lag sequences. Each preset shares one traffic selection across its named steps. Choose added delays directly, or specify target pings and select a local baseline profile for your server. **Lowest available** means no added delay, independent of baseline.

Loading selects step 1 without starting capture. Next, Previous, and Reset use the same controller from buttons and hotkeys. Saved edits do not change a loaded sequence until you load them. Your baseline stays local; exported presets retain their original targets. Hotkey actions pause while editing presets/profiles or using import/export dialogs.

See the [preset guide and manual checks](docs/PRESETS.md), or import [the four-leap example](examples/four-leaps.json). With baseline 50 ms, it adds 150, 0, 100, then 50 ms inbound. A target below your baseline adds zero and is marked as unreachable; the original target remains visible.

## Planned features

- Timed sequence playback with customizable time per step.
- A searchable preset launcher.
- Optional mouse-wheel adjustment of added delay.
- A customizable home screen.

## Development

See [repository setup](docs/REPOSITORY-SETUP.md), [build status](docs/BUILDING.md), and [roadmap](docs/ROADMAP.md).

Run `scripts\test-core.cmd`, `scripts\test-lifecycle.cmd`, `scripts\test-hotkeys.cmd`, and `scripts\test-presets.cmd` to check capture behavior, hotkeys, preset calculations, persistence, and editor callbacks. These scripts use the Visual Studio C++ Build Tools. Also test shortcuts while the target game is focused, including holding a key and switching between Start, Stop, and Toggle.

## Source layout

- `src/core/`: plain C settings, preset JSON/calculations, sequence controller, action identifiers, and input matching. No Windows or IUP headers.
- `src/backends/windows/`: the WinDivert adapter, structured traffic-filter translation, and Lag queue. `legacy/` contains the inherited packet machinery and remaining Windows-only effects, including their older UI coupling.
- `src/platform/windows/`: global input listening, Windows key names/settings files, preset/profile storage, and elevation.
- `src/ui/`: the IUP application, preset/profile editors, sequence controls, and Lag controls. The current Lag knob still edits both direction delays together; the model/backend support independent values.

The controller owns accepted Lag settings and queries the backend for running state. Failed live updates leave accepted settings unchanged. Backends implement start, stop, and applying Lag conditions; the shared interface does not expose WinDivert packets or handles. Settings can select traffic structurally or explicitly name a native filter backend. The existing filter editor uses the `windivert` native filter option.

**Changing the active Lag configuration immediately changes the rules governing packets that Clumsier is already holding.** Held packets retain their original enqueue times: lowering the delay can release them sooner, and raising it can hold them longer. Their waiting time does not restart when settings change. This makes live changes control the current queue as well as newly arriving packets.

Here, "immediately" means the new rules apply on the next processing pass, not that the settings call synchronously delivers packets. Disabling Lag or a direction releases the affected queued packets on processing; Stop flushes the queue. Traffic selection changes still require stopping capture first. Failed updates leave the previous rules in effect. The Windows queue retains its inherited overload limit. Other backends will need to document their scheduling precision and overload behavior.

This is an operational Windows application with a portable core, not a Linux/macOS release. Windows input IDs and filesystem access stay in the Windows adapter; preset JSON and sequence behavior live in the portable core. A future platform must supply its own key mapping and storage integration. The remaining legacy UI coupling is intentionally isolated rather than generalized into the portable API.

## License

The original [MIT license](LICENSE) is retained; original Clumsier additions are also provided under MIT. Bundled dependencies retain their separate licenses. Release packaging must include the appropriate notices and meet their redistribution requirements.

Preset JSON uses [cJSON 1.7.19](external/cjson/README.clumsier.md), with its [MIT notice](external/cjson/LICENSE) retained.

The Qt preview includes selected [Tinted Theming palettes](third_party/tinted-schemes/README.md), with their [MIT notice](third_party/tinted-schemes/LICENSE) retained. Its theme picker is inspired by Monkeytype and implemented independently.
