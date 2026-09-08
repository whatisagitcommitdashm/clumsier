# Clumsier

A Windows network-condition tool evolving from Clumsy, with planned customizable hotkeys, reusable sequences, and a cleaner interface.

**Initialization.** Application code is still the inherited version; planned features are not implemented and the build has not yet been verified for this project.

## Origins and attribution

Independently maintained by [niko (whatisagitcommitdashm)](https://github.com/whatisagitcommitdashm), starting from [Auzgame/clumsy0.3-with-keybinds](https://github.com/Auzgame/clumsy0.3-with-keybinds), itself derived from [jagt/clumsy](https://github.com/jagt/clumsy) by Chen Tao and contributors. Original Git history and license notices are preserved. This is not an official release from either upstream project.

Baseline commit: `357e1e67a5803ffdd7d483b463575508a0a5f578`.

## Planned features

- Reliable Start, Stop, and Toggle behavior.
- Saved presets and arbitrary-length sequences of named steps.
- Configurable global hotkeys and a searchable preset launcher.
- Optional mouse-wheel adjustment of added delay.
- Preset import/export and a customizable home screen.

## Development

See [repository setup](docs/REPOSITORY-SETUP.md), [build status](docs/BUILDING.md), and [roadmap](docs/ROADMAP.md).

The inherited F5 start hotkey seems to disrupt connectivity when pressed repeatedly. My initial priority is to better understand this bug, then to find and test a solution.

## License

The original [MIT license](LICENSE) is retained; original Clumsier additions are also provided under MIT. Bundled dependencies retain their separate licenses. Release packaging must include the appropriate notices and meet their redistribution requirements.
