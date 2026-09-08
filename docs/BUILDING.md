# Build status

The inherited application has not yet been built or run for Clumsier. 

## Existing inputs

- `genie.lua`: GENie generation for Visual Studio and a MinGW route using Clang; references a known upstream GENie revision.
- `build.zig`: legacy Zig build APIs and a Windows SDK resource compiler path. A compatible Zig version must be established.
- `external/`: bundled IUP libraries and WinDivert 2.2.0 distributions.
- `etc/`: resources and default filtering configuration.

On 2026-09-07, Git, VS Code, GCC, and CMake were discoverable on the command path. Zig, Clang, and the Visual C compiler were not. Tools may still exist outside that path or in a Visual Studio developer environment.

## Next steps

1. Inspect available Windows SDK/compiler installations.
2. Choose one reproducible 64-bit build route and record exact versions.
3. Build the unchanged baseline.
4. Record dependency packaging and administrator requirements.
5. Reproduce repeated-start behavior with controlled traffic and document a regression procedure.

Successful compilation alone does not establish interception or connection-recovery correctness.
