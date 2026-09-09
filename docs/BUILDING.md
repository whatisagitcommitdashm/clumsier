# Building and checking Clumsier

The supported local build is a Windows x64 debug build using Visual Studio C++ Build Tools and the bundled IUP/WinDivert libraries.

From the repository root, run:

```powershell
.\scripts\build-windows.cmd
```

The script locates Visual Studio through `vswhere`, selects its x64 tools, compiles the application and resources, and copies the required DLLs, driver, and default filter configuration. Output is `bin/baseline-msvc/clumsy.exe`; the directory name is retained for existing local debugging setups. Close an already running copy before rebuilding that executable. The old local `work/build-baseline.cmd` has also been updated for the new layout.

The application requests elevation when launched because WinDivert needs administrator access. Building and running the automated tests do not require elevation.

## Automated checks

```powershell
.\scripts\test-core.cmd
.\scripts\test-lifecycle.cmd
.\scripts\test-hotkeys.cmd
```

- `test-core` compiles the shared core without Windows or IUP headers, tests the controller with a fake backend, and tests the Windows filter translator and Lag queue without intercepting network traffic.
- `test-lifecycle` exercises the existing capture workers with a simulated WinDivert driver.
- `test-hotkeys` checks matching, recording, persistence, forwarding, and actual listener startup/shutdown. It does not inject keyboard input or start packet capture.

The core tests can also be compiled with a standard C11 compiler, independently of the Windows application:

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic -Isrc tests/core.c src/core/actions.c src/core/controller.c src/core/network.c src/core/hotkey_matcher.c -o core-tests
```

The core has been checked with MSVC and the locally installed MinGW GCC. This is not a Linux or macOS backend test; neither backend exists yet.

## Other build definitions

`genie.lua` and `build.zig` have updated source paths and include directories. The legacy Zig configuration still needs its compatible toolchain established; these routes have not been verified by this refactor. `external/` contains the bundled dependencies, and `etc/` contains the resources and initial filter configuration.

## Manual regression checks

After a successful build, check Start/Stop/Toggle, recording and saved hotkeys, Lag enable/disable, direction selection, and editing delay while capture runs. Confirm Stop restores normal traffic, including after a high-delay setting. Check another inherited effect as well. Automated tests do not establish actual in-game timing or GUI correctness.
