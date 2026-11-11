# Changelog

## macOS 0.1.0-beta.4

### Fixed
- Global hotkeys could remain paused after retrying the listener from the Hotkeys page. Recording worked, but actions did not fire after returning to the game. Closing the listener now clears its previous session state.
- Explicitly initialize the running macOS app's Dock icon from the bundled axe artwork instead of relying only on Finder/Launch Services icon discovery.

### Distribution
- Updated Apple Silicon (macOS 15.7+) and Intel (macOS 12+) builds from the same source revision.
- Application bundle build number is now 4. These remain experimental, development-signed, non-notarized builds; Intel still needs real-hardware acceptance.
- The hotkey and Dock fixes were verified by the Apple Silicon tester. No change to the networking helper or saved bindings.
