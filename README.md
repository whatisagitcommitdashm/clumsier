# Clumsier

A Windows network-condition tool evolving from Clumsy, with configurable hotkeys and plans for reusable sequences and a cleaner interface.

**Development status.** Start and Stop have been stabilized and tested. Configurable global hotkeys are implemented; sequences and the other planned features below are still to come.

## Origins and attribution

Independently maintained by [niko (whatisagitcommitdashm)](https://github.com/whatisagitcommitdashm), starting from [Auzgame/clumsy0.3-with-keybinds](https://github.com/Auzgame/clumsy0.3-with-keybinds), itself derived from [jagt/clumsy](https://github.com/jagt/clumsy) by Chen Tao and contributors. Original Git history and license notices are preserved. This is not an official release from either upstream project.

Baseline commit: `357e1e67a5803ffdd7d483b463575508a0a5f578`.

## Global hotkeys

The defaults are **F5: Start**, **F6: Stop**, and **F7: Toggle**. Start leaves an already running capture running; Stop leaves a stopped capture stopped. Toggle switches between those states.

Click a binding field or its **Record** button. Hold the desired keys and/or mouse buttons together, then release them. The first release fixes the combination; recording finishes once all inputs are released. The opening click is excluded. Click **Cancel** to discard a recording; Escape itself can be recorded. Normal hotkey actions are suspended during recording.

Choose **Apply & Save** to activate the edited bindings and remember them across restarts. **Clear** unassigns a binding in the editor. **Show defaults** fills in F5/F6/F7; both changes also require Apply & Save.

Supported combinations include bare letters or digits (`W`, `7`), several ordinary keys (`Q + E`), modifiers, and mouse buttons (`Ctrl + Mouse4`). Mouse1/2/3 mean left/right/middle; Mouse4/5 are the two extra buttons. Left and right modifiers are interchangeable. Function keys, navigation keys, punctuation, and other Windows virtual keys are also supported. Uncommon keys use a stable `KeyXX` label; punctuation labels follow US key names. Keyboard hardware and Windows shortcuts can limit which combinations are observable. Mouse-wheel bindings and sequences of separate presses are not implemented yet.

**Input passes through:** Clumsier forwards keyboard and mouse presses and releases, so binding W does not block movement in the game. Other applications can also act on the same combination. This replaces the previous exclusive Windows hotkey registration, so Clumsier no longer reports another application's registered shortcuts as conflicts.

A combination fires when all its inputs are down, regardless of their press order. Extra held inputs are allowed, so movement keys do not block another hotkey. Holding a combination does not repeat it; releasing and pressing one of its members again rearms it. Two bindings cannot be identical or contain one another (for example, `Q` and `Q + E`). Otherwise both matching bindings may fire if their inputs are held together. Hotkeys are global, including while Clumsier is focused; recording is the exception.

Settings remain in `%LOCALAPPDATA%\Clumsier\hotkeys.ini`. Existing version-1 settings load automatically; Apply & Save writes version 2. Failed validation or saving leaves the active bindings unchanged. Invalid files are reported and preserved until you explicitly save replacement settings. Bindings remain separate from packet-filter presets.

## Planned features

- Saved presets and arbitrary-length sequences of named steps.
- A searchable preset launcher and hotkeys for navigating sequences.
- Optional mouse-wheel adjustment of added delay.
- Preset import/export and a customizable home screen.

## Development

See [repository setup](docs/REPOSITORY-SETUP.md), [build status](docs/BUILDING.md), and [roadmap](docs/ROADMAP.md).

Run `scripts\test-lifecycle.cmd` and `scripts\test-hotkeys.cmd` to check capture lifecycle behavior and hotkey actions, parsing, persistence, and rebinding. These scripts use the Visual Studio C++ Build Tools. Also test shortcuts while the target game is focused, including holding a key and switching between Start, Stop, and Toggle.

## License

The original [MIT license](LICENSE) is retained; original Clumsier additions are also provided under MIT. Bundled dependencies retain their separate licenses. Release packaging must include the appropriate notices and meet their redistribution requirements.
