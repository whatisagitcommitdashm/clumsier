# Roadmap

## 1. Baseline and reliable lifecycle

Establish a reproducible 64-bit build. Part of the motivation for this project is due to an unintended bug present in the inherited keybind version where pressing the start hotkey twice caused major issues. My initial priority is to investigate this bug, then implement consistent Start, Stop, and Toggle behavior and cleanup after partial startup failures. Verify repeated and rapidly alternating commands with controlled traffic.

## 2. Shared commands and live settings

Separate input/UI actions from capture lifecycle. Define queued-packet behavior when delay changes. Buttons and hotkeys should use the same commands; delay changes should not restart capture.

## 3. Presets and sequences

Save configurations and arbitrary-length named steps. Add configurable shortcuts, current/next feedback, and hold/loop/stop ending behavior. Loading selects step one; activation is explicit by default. Verify a four-step sequence, reversal, restart, stop, and persistence.

## 4. Sharing

Versioned JSON import/export with validation, preview, and duplicate handling. Personal hotkeys and layout remain local by default. Imports never activate automatically. Verify round-trip fidelity and invalid-file handling.

## Timed sequence playback

Add optional automatic advancement with a customizable duration for each step, so a player can start a sequence at the beginning of a game and let it run. Keep manually advanced sequences supported. Include timing in shared presets, while baseline profiles and hotkeys stay local.

Before implementation, settle how Play/Pause, capture Stop, manual Next/Previous, Reset, and sequence switching affect the timer. Define the final-step behavior explicitly (hold, repeat, or stop), show the time remaining, and use a monotonic clock so wall-clock changes cannot skip steps. Failed step changes should pause playback and retain the accepted state. Test timers with a fake clock, including delayed UI callbacks, and verify actual transitions in game. This is a future milestone, not part of the current UI pass.

## 5. Launcher and scrolling

Search presets by name. Add bounded wheel adjustment with configurable increments and visible status. Wheel edits are temporary unless saved. Verify focus restoration and input conflicts in the intended game environment.

## 6. Interface and release

Prototype the home screen before choosing its implementation. Add compact view, favorites, themes, and section customization. Verify packaging, dependency notices, and connection recovery before a documented release.
