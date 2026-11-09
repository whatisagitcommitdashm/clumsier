# Experimental local macOS lag tool (no Apple membership)

The local PF/dummynet backend can now be controlled by the Qt app, or used
as a standalone terminal tool.
It uses macOS PF/dummynet with administrator authorization. It does not require
Developer ID, provisioning profiles, disabling SIP, or system-extension developer
mode. It currently needs Python 3 (already present in the development environment).

**Status:** fake-runner lifecycle tests and native PF syntax parsing pass.
The user also reports successful in-game use against the practice server with
outbound TCP delay at 100 ms and 300 ms, returning to zero with `off`, and a
150 ms / 3-second burst after the countdown. This is qualitative gameplay
acceptance, not an instrumented latency measurement or full backend acceptance.

Inbound/bidirectional delay, orderly shutdown recovery, existing connections,
VPNs, sleep/wake, and retained-packet update semantics remain unverified. Keep
the terminal open and test in practice before an important run. No packet loss
or bandwidth cap is added.

## Use the Clumsier interface

First quit the standalone `macos_lag.py` session with `quit`; do not run two
conditioning sessions against the same connection. Build the Qt application:

```sh
cmake -S src/ui/qt -B build/qt-macos -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/qt-macos --parallel 4
```

In Terminal, authorize the helper for **one server endpoint**:

```sh
sudo "$(command -v python3)" tools/macos_lag_helper.py --server 15.204.237.8 --port 25565
```

Keep that Terminal open. In a second Terminal, open the GUI as your normal user:

```sh
open build/qt-macos/Clumsier.app
```

The macOS GUI now defaults to PF/dummynet. Start/Stop, quick delay, sequences,
and the existing global hotkeys control the helper. Choose outbound delay to
repeat the standalone test; reconnect Minecraft after starting capture if needed.
Delay is limited to 0–1000 ms per direction. Settings > Advanced mode explains
the queue-behavior caveat. No background input grant is needed for mouse controls;
global hotkeys still need Input Monitoring.

An empty/Any sequence target means the helper's **authorized TCP endpoint only**,
not system-wide traffic. Explicit IP/port selections must match that endpoint;
UDP and native Windows filter syntax are rejected. The helper fixes the endpoint
for its lifetime, so changing server requires quitting and relaunching it.

The GUI stays unprivileged. The root helper listens on a per-user, owner-restricted
Unix socket under root-owned `/var/run`, accepts one GUI connection, and attempts
cleanup on disconnect or ten seconds without traffic. The GUI sends heartbeats.
Stop removes capture but leaves the helper connected so Start works again. Closing
the GUI ends the helper; relaunch the helper for another GUI session. Ctrl-C also
attempts cleanup. A crash/SIGKILL can leave resources: retain recovery commands.
A stale socket causes refusal rather than automatic deletion; inspect for a live
helper before manually removing its exact socket path.

For the signed Network Extension instead, launch the packaged executable with
`--network-extension`. The default local mode does not require Apple membership.
The integrated path has fake-broker command/acknowledgement tests. The user
also reports successful gameplay through the GUI, including global hotkeys and
the native HUD overlay after its macOS stacking fix. This is qualitative manual
acceptance, not instrumented queue timing, permission recovery, or crash testing.

## Try the practice server

From the repository root, first inspect the generated rules without changing anything:

```sh
python3 tools/macos_lag.py --server 15.204.237.8 --port 25565 --direction out --dry-run
```

Then start a session (enter your password locally at the sudo prompt):

```sh
sudo /absolute/path/to/python3 tools/macos_lag.py \
  --server 15.204.237.8 --port 25565 --direction out
```

Find the interpreter's absolute path with `command -v python3`. Connect or
reconnect Minecraft after startup. The session starts at **zero added delay**.
The IP above is the workspace's practice-server target, **not Mineplex**.
Use the actual numeric IP and port of your Minecraft TCP connection for Mineplex
or another practice server. A hostname may use SRV records, multiple addresses,
or a proxy: resolving its name alone does not prove the selected endpoint is right.

Commands typed in this terminal:

```text
delay 100           # constant 100 ms outgoing delay
off                 # zero added delay; session remains ready
burst 150 3         # wait 5 seconds, add 150 ms for 3 seconds, then zero
burst 150 3 10      # same, with 10 seconds to refocus the game
status              # dummynet counters; includes other tools' pipes too
quit                # detach owned rules, drain best-effort, remove owned pipes
```

Ctrl-C, EOF, SIGHUP and SIGTERM attempt the same scoped cleanup. A failed command
also exits through cleanup. SIGKILL, a process crash or machine failure cannot run
Python cleanup: copy the printed recovery commands somewhere accessible.
Never run `pfctl -F all`, `pfctl -d`, or `dnctl flush` to clean up this tool.
Those affect other applications.

`--direction in` delays inbound traffic; `--direction both` delays both.
**Values are added delay per selected direction, not target ping.** Both directions
at 100 ms can add roughly 200 ms to RTT. Begin conservatively and test separately.
The burst countdown is a stopgap, **not a global hotkey or hold-to-lag control**.
The standalone terminal tool does not accept Qt commands; use the helper above
for Qt hotkeys.

## Ownership and limitations

- Only TCP packets matching one numeric remote IP and server port are selected.
- Requires the active `dummynet-anchor "com.apple/*"` hook; otherwise refuses startup.
  It does not reload `/etc/pf.conf`, alter it, or install a new global hook.
- Each session uses a random dedicated child anchor and two checked, random pipe
  IDs, plus a PF enable reference released with its own token. Do not run another
  allocator targeting the same pipe IDs during startup; allocation is not atomic.
- Never flushes global states, global rules or other tools' pipes.
- Pipe updates are sequential, not atomic across directions. A failed update exits
  and cleans up rather than claiming the previous configuration was preserved.
- Reconfiguring retained packets and releasing queued traffic on Stop are native
  dummynet behavior, not yet proven to match Clumsier's scheduler contract. Cleanup
  sets owned pipes to zero, waits briefly and deletes them; retained packets may
  be lost. Deleting a pipe is not a demonstrated lossless drain.
- A detached rule does not by itself prove an existing PF state no longer refers
  to a pipe. Start/reconnect, Stop, and packet delivery need real acceptance tests.
- This process runs as root. Inspect the script, run it only from a trusted checkout,
  and do not grant broad passwordless sudo access to Python or this repository.

Tests (no root or network changes):

```sh
python3 -m unittest discover -s tests/macos -p 'test_local_lag.py' -v
```

## HUD on macOS

The HUD uses a native floating window level, stays visible when Clumsier is
inactive, and is configured to join all Spaces, including full-screen auxiliary
spaces. It does not activate Clumsier or request keyboard focus. Native window
property checks pass, and the user reports the HUD and hotkeys now work in
gameplay. Full-screen variants and multiple displays still need wider testing. Enable HUD in the GUI, switch
to Minecraft, and test both windowed and full-screen modes. If full-screen mode
still hides it, report the macOS version and Minecraft window mode; windowed
mode is the immediate fallback.
