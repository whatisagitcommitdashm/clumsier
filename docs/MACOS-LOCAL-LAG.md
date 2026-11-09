# Experimental local macOS lag tool (no Apple membership)

This is a standalone **terminal experiment**, not the Qt application's backend.
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
The Qt hotkeys currently control the Network Extension backend, not this script.

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
