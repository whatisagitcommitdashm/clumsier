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

## Background launch (development checkout)

Build the Qt app once using the commands below. Quit any previous Clumsier or
standalone lag session, then run this as your normal user, **not** under sudo:

```sh
sh scripts/start-macos-local.sh
```

The script requests your administrator password through sudo, starts a background
helper allowing TCP conditioning without a server selection, waits for readiness, and opens Clumsier.
You may close Terminal after it says the background helper is ready. The GUI is
never run as root. No launchd service, login item, sudoers rule, or installer is
created. This is still a development launcher requiring Python and a trusted
checkout; it is not the future downloadable package. Authorization executes the
helper code from that checkout with root privileges, just like the previous
manual command. Do not use an untrusted or shared-writable checkout/interpreter.

The helper waits up to 60 seconds for the GUI connection. After connecting it
clears capture on EOF or ten seconds without a heartbeat, including while capture
is stopped. In background mode it then waits up to 60 seconds for another GUI
connection before exiting. Reopening never restores lag automatically. Re-running
the launcher while its socket exists opens the GUI without creating a second
helper; the ORIGINAL authorized scope remains in effect. If a previous launch used
a server restriction, it remains restricted. To change launch scope,
quit the GUI and wait for the helper to exit before launching with a new endpoint. Stop in the GUI clears capture while keeping the connection usable.
Sleep/wake can expire the heartbeat; restart the launcher if disconnected.

Startup failure is reported before the GUI opens. Runtime errors and exact
resource recovery commands are logged to `/var/run/clumsier-lag-USER_ID.log`
(root-owned mode 600); inspect with:

```sh
sudo tail -100 "/var/run/clumsier-lag-$(id -u).log"
```

Logs append across sessions so recovery information survives a crash. They are
not automatically removed or rotated yet. If the helper crashes, use only the
specific anchor/pipe/reference recovery commands from the relevant session.
An existing socket is never silently deleted or probed with a throwaway
connection (which could disrupt the real GUI). A stale socket can still prevent
connection; inspect the recovery log if Start fails after reopening.
Do not remove it until the previous
helper has exited. Forced termination and power loss can still strand PF state.

## Use the Clumsier interface

First quit the standalone `macos_lag.py` session with `quit`; do not run two
conditioning sessions against the same connection. Build the Qt application:

```sh
cmake -S src/ui/qt -B build/qt-macos -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build/qt-macos --parallel 4
```

In Terminal, authorize the helper (no server required):

```sh
sudo "$(command -v python3)" tools/macos_lag_helper.py
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

**Default scope is all TCP traffic in IPv4 and IPv6, not only Minecraft.**
An empty/Any sequence target means any remote IP and port. Direction and delay
are controlled in the GUI. Other applications may slow down too; UDP (including
QUIC) is not conditioned. Existing advanced traffic IP/port fields can narrow a
sequence target. Native Windows filter syntax and UDP requests are rejected.
Optional CLI restrictions remain available with `--server IP --port PORT` (or
`sh scripts/start-macos-local.sh IP PORT`); GUI requests cannot widen those limits.
No persistent server-selection onboarding is required.

The GUI stays unprivileged. The root helper listens on a per-user, owner-restricted
Unix socket under root-owned `/var/run`, accepts one GUI connection, and attempts
cleanup on disconnect or ten seconds without traffic. The GUI sends heartbeats.
Stop removes capture but leaves the helper connected so Start works again. Closing
the GUI clears capture; background mode permits reopening within 60 seconds. Ctrl-C also
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

- TCP traffic is selected across IPv4/IPv6 by default. Advanced numeric IP/port
  filters and optional CLI authorization restrictions can narrow that scope.
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

## Manual acceptance: server-free TCP mode

This broader mode has automated rule/validation coverage, but has not yet had
live gameplay acceptance. Quit the old GUI/helper and wait for its socket to
expire before launching without arguments, so an old server restriction is not
reused. Keep Stop and recovery commands available. Do not test during important
uploads, calls or a remote-control session.

1. Launch `sh scripts/start-macos-local.sh` with no server argument. In quick
   controls use 0 ms, Start, and confirm normal play on both practice and Mineplex.
   Try first with an existing game connection, then reconnect if necessary; report
   that distinction. No server entry should be required when switching servers.
2. Test 50/100/300 ms outbound, inbound, then both separately. Both at 100 ms is
   per-direction delay, not 100 ms total RTT. Confirm changes, zero, Stop/Start,
   sequences and keyboard hotkeys behave as expected, without duplicate actions.
3. During a small disposable TCP download/request, confirm it is affected too
   and recovers at zero/Stop. A browser may use QUIC/UDP and is not reliable proof.
   Check normal browsing/DNS/game connection after Stop; all TCP scope is intended.
4. Quit via the main window close button while delay is active: the game should
   remain open, connection should recover, and reopening within 60 seconds should
   start stopped. After quitting for 60 seconds, launcher should start a new helper.
5. Test a forced GUI exit (not SIGKILL of the root helper): lag should clear on
   disconnect, or within the 10-second heartbeat limit if the GUI is unresponsive.
   Check sleep/wake separately; reconnect/relaunch may be necessary.
6. If available, test IPv6 TCP and your usual VPN. Otherwise mark them untested,
   not passed. Keep the existing advanced filter checks separate from default use.

Report server, direction, delay, whether the connection predated Start, any
unexpected disconnect, and whether Stop/quit restored traffic. Never flush PF
or all dummynet pipes to recover; use only this session's logged commands.
