# Linux and macOS development

The current milestone is an end-to-end Lag-only prototype for each platform.
Linux has a terminal frontend and a native backend; macOS has a small host app
and a packet-filter extension. Both are intended to load shared presets, apply
their steps to real traffic, and stop cleanly. Linux now passes native compilation and isolated packet acceptance checks on
Fedora; see [the validation report](LINUX-VALIDATION.md). macOS still needs native
compilation and packet testing on its target OS. They are separate from the supported Windows application.

## Ownership during parallel development

The Linux work lives in `src/backends/linux`, `src/platform/linux`, `tests/linux`,
its platform scripts, and `docs/LINUX.md`. The macOS work uses the equivalent
`macos` paths and `docs/MACOS.md`. The portable terminal frontend lives in
`src/prototype`. Changes to that frontend, the shared core, Windows implementation,
UI, preset format, and common build definitions are reviewed centrally.

The agents currently share a checkout, so these directory boundaries prevent
overlapping edits. They do not provide the isolation of separate Git worktrees.
Agents must not stage, commit, or push; the project owner reviews and publishes
the work.

## What a backend must preserve

`src/core/network.h` defines the accepted behavior. In particular:

- Start must clean up after a partial failure. Repeated lifecycle commands must
  not leave capture running unexpectedly or strand traffic.
- A rejected settings change must leave the previous settings in effect.
- Accepted delay changes apply to packets already held, keeping their original
  arrival times. A lower delay can release them sooner; a higher delay can hold
  them longer. Zero delay or disabling a direction releases its held packets.
- Stop must release held traffic and dispose of owned resources.
- Traffic selections cannot change during capture. Unsupported filters must be
  rejected explicitly, rather than capturing different traffic without notice.

Any limits in meeting these requirements must be documented. In particular,
losing access to a kernel queue or forcibly terminating a provider may lose held
packets even when orderly shutdown works. Passing scheduler tests alone does
not establish that an operating system's capture mechanism meets the contract.

Linux uses the existing synchronous controller through `NetworkBackendOps`.
The Mac host uses asynchronous provider acknowledgements, so it commits its
displayed step only after a successful reply. Both reuse the shared preset parser
and delay calculations. Neither prototype includes global hotkeys or the full
Windows editor; presets can be prepared in Windows or edited as JSON.

## Evidence required before integration

Each platform needs deterministic tests for its delay and ownership logic,
native compilation, and tests with actual packets. The native checks must cover
live increases and decreases, zero delay, traffic selection, repeated Start/Stop,
partial startup failures, queue overload, and shutdown. Crash recovery needs its
own verification; successful normal shutdown does not prove it.

Tests must distinguish code exercised with fake packets or callbacks from real
network behavior. Any experiment that changes network rules belongs in an
isolated test environment and must clean up only resources it owns.

The prototypes were initially developed on Windows without WSL. Linux native
checks now run on the project owner’s Fedora desktop in isolated namespaces. macOS
SDK, deployment, and packet checks still need a Mac. Platform reports record
exactly which checks ran and which remain.

Once the experiments establish the actual OS constraints, we can decide which
queue logic should move into the shared core. Preset calculations and sequence
navigation already belong there and should not be duplicated in each port.
