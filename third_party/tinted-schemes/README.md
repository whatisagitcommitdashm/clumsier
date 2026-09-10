# Tinted Theming palettes

Selected Base16 schemes from https://github.com/tinted-theming/schemes,
revision `fdca32a0d14ec80ad83a78a9ccb85592ca6cb9e1` (spec-0.11).
The upstream MIT license is retained in `LICENSE`; individual authors remain
listed in each unmodified YAML file and the generated catalog.

Clumsier maps base00 to background, base01 to surface, base05 to text,
base0E to accent, and base0B to success. Secondary text and borders are blended
from the foreground and background for UI readability. These are adaptations
of the color palettes, not reproductions of the gallery's syntax highlighting.

Run `python scripts/generate-theme-catalog.py` from the repository to regenerate
`src/ui/qt/TintedThemes.js` after changing this selection. The generated file is
checked in: building and running the app require neither Python nor a network
connection. The build script copies the license beside the preview executable.
