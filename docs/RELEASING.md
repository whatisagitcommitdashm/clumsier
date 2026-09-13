# Windows beta releases

The first release is `0.1.0-beta.1` (see `VERSION`). Update that file for each
new beta. This document describes packaging, not a completed security review.

## Prepare the downloads

From a reviewed, committed checkout, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-beta.ps1
```

The packager requires Git, Visual Studio C++ Build Tools, CMake, and Qt 6.10.3
MSVC 2022 x64. It finds the local or standard Qt installation automatically;
`-QtRoot 'D:\Qt\6.10.3\msvc2022_64'` selects another location.
The first run downloads pinned dependency sources to `work/release-cache` and
verifies their SHA-256 hashes. Later runs can reuse this cache offline.

Before committing, add `-Preview` to rehearse. Source archive filenames and
`BUILD-INFO.json` explicitly identify uncommitted source. Do not publish those
as an exact-commit release. Preview captures non-ignored working files; release
mode uses `git archive HEAD`. Both compile the same snapshot that they package.

Each run creates a new folder under `dist/` containing:

- `clumsier-beta-0.1.zip`: app, Qt libraries, upstream WinDivert
  driver/DLL, Microsoft runtime installer, licenses, and beginner instructions.
- `clumsier-<version>-source.zip`: matching Clumsier source and the double-click
  build helper. It includes tests, but no personal settings or development kit.
- `clumsier-<version>-dependency-sources.zip`: matching Qt and WinDivert source
  archives with origin URLs and checksums. Offer this beside the binary ZIP.
- `SHA256SUMS.txt`, plus build and test logs. Every ZIP also includes file hashes.

The app is built in a fresh directory. Qt shell/bridge tests run against that
snapshot. A deployment load check removes SDK paths before loading the embedded
QML. No capture occurs in that check. Core, lifecycle, hotkey and preset test
scripts should also pass before a release. No script commits, tags, pushes,
publishes, changes antivirus settings, or sends messages to testers.

The Windows ZIP extracts to `clumsier-beta-0.1/`, containing only the
`clumsier-beta.exe` launcher and an `app/` folder. Libraries, licenses, build
information, instructions, and the runtime installer live inside `app/`.
Keep both items together. The launcher has no Qt or Visual C++ runtime dependency.

## Final manual checks

On a Windows x64 machine without the development tools (or a suitable clean VM):

1. Extract the runnable ZIP. Try launch, then install the included Microsoft
   runtime if needed. Check the initial Mineplex baseline prompt and all three
   starter sequences. Do not copy a developer's AppData directory into the test.
2. Check Start/Stop, switching and editing while running, hotkeys, and the HUD
   over the game. Closing must release traffic and exit the process.
3. Restart and confirm settings remain. Extract an update into a separate folder
   and confirm the same library is used without overwriting edits.
4. Extract the source ZIP separately and double-click `Build Clumsier.cmd`.
   Verify prerequisite guidance and the completed build on a nondeveloper setup.
5. Inspect the archive contents, notices, exact commit and hashes. Treat any
   antivirus detection as something to investigate, not a reason to disable it.

A developer-machine load check does not establish any of those clean-machine
or in-game results. Local compilation also still relies on Qt, the compiler,
and the upstream prebuilt WinDivert driver.

## Publish after review

1. Review and commit the intended source changes, then push that commit.
2. Run packaging again without `-Preview` from that clean checkout.
3. On the repository's GitHub Releases page, draft a release using a tag such as
   `v0.1.0-beta.1` targeting that exact commit. Mark it as a pre-release.
4. Upload all three ZIPs and `SHA256SUMS.txt`. Include the build/test logs if
   desired; the checksums cover them too. Review the draft before publishing.
5. Send testers the release link, pointing most people to the Windows x64 ZIP
   and source-build users to the source ZIP. Ask for feedback by DM.

Suggested release description:

> Clumsier Windows beta: configurable delay, sequences, global hotkeys and an
> in-game HUD. Includes Skylands, Pirate Bay and Good Basic; enter your own
> Mineplex baseline on first launch. Download the Windows x64 ZIP to run, or the
> source ZIP to build with the double-click helper. This is an unsigned beta;
> administrator permission is required for network capture. Please report
> issues and steps to reproduce them to the person who invited you to test.

Keep checksums with the downloads, but do not describe hashes, local builds, or
public build logs as proof of safety. No code signing or reproducible-build
guarantee is set up yet. Automated GitHub release builds can follow later.
