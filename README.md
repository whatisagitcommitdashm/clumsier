# Clumsier

A Windows network-condition tool evolving from Clumsy, with configurable hotkeys and plans for reusable sequences and a cleaner interface.

**Development status.** Start and Stop have been stabilized and tested. Configurable global hotkeys are implemented; sequences and the other planned features below are still to come.

## Origins and attribution

Independently maintained by [niko (whatisagitcommitdashm)](https://github.com/whatisagitcommitdashm), starting from [Auzgame/clumsy0.3-with-keybinds](https://github.com/Auzgame/clumsy0.3-with-keybinds), itself derived from [jagt/clumsy](https://github.com/jagt/clumsy) by Chen Tao and contributors. Original Git history and license notices are preserved. This is not an official release from either upstream project.

Baseline commit: `357e1e67a5803ffdd7d483b463575508a0a5f578`.

## Global hotkeys

For now, the implementation has 3 customizable keybinds: Start, Stop, and Toggle. Start leaves an already running capture running, or starts one if none are running. Stop leaves a stop capture stopped, or stops an active one. Toggle toggles between those states. To customize, type a combination for each action and choose "Apply & Save". 

Duplicate bindings are rejected. If registering or saving new bindings fails, the previous bindings stay active. If registration fails at startup, no global hotkeys are enabled; the panel explains the conflict so you can edit the bindings. The on-screen Start/Stop button remains available.

Settings are stored in `%LOCALAPPDATA%\Clumsier\hotkeys.ini`, independently of packet-filter presets. **Show defaults** only fills the editor; choose **Apply & Save** to activate and save them. An invalid settings file is reported and left untouched until you explicitly save replacement settings.

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
