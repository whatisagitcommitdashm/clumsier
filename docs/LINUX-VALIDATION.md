# Linux native validation — 2026-09-11

Tested on Fedora 42 KDE, x86_64, kernel 6.16.10-200.fc42.x86_64,
GCC 15.2.1, libnetfilter_queue 1.0.5, nftables 1.1.1, and iputils 20250605.
Starting point: cross-platform branch commit `453382d`; this report includes
local changes made during Linux validation.

## Build and deterministic tests

`scripts/build-linux.sh` compiles the complete terminal application with strict
C11 warnings treated as errors. `--help` runs successfully. Missing development
files now produce an actionable package-install message instead of silent exit.
Linux queue/rules/session tests and portable console tests pass. The shared core
controller/matcher test also passes when compiled with the documented C11 command.

## Real network tests

`scripts/test-linux-network.sh` runs the real binary in fresh user/network
namespaces, using a veth pair with IPv4 and IPv6 addresses and no external route.
It retains an unrelated nftables table to verify cleanup ownership. TCP/UDP echo
servers run in the peer namespace. All subprocesses are stopped during cleanup;
unnamed namespaces are released when the last process exits.

The suite checks:

- Four-leap preset at baseline 50: measured approximately 150, 0, 100, and 50 ms.
  Loading alone installs no rules. Previous/Reset restore the expected step.
- Twenty cycles of repeated Start/Stop, preserving unrelated tables.
- Zero delay and Stop release a packet held under a five-second delay promptly.
- Raising delay from 1200 to 2200 ms after 500 ms holds the packet for approximately
  2200 ms from receipt, rather than restarting its clock.
- Invalid delay input leaves the accepted live delay intact.
- Inbound, outbound, and combined delay, numeric IPv4/IPv6 address selection,
  and exclusion of another address.
- TCP/UDP remote-port selection for IPv4/IPv6, including unaffected alternate
  ports and protocols; echo payloads remain unchanged.
- EOF, SIGINT, and SIGTERM release held traffic and clean up tables.
- Independent concurrent sessions, including continued delay after one stops.
- Start without CAP_NET_ADMIN fails clearly and installs no rules.
- Invalid addresses and Windows native filters fail without capture.
- A 1400-datagram burst exceeds the 1024-packet capacity: excess traffic can pass
  before its delay, held traffic drains on zero delay, and normal traffic recovers.
  This is a bounded overload check, not a lossless-throughput guarantee.
  The recorded run received 376 echoes early and all 1400 after draining.
- SIGKILL may lose held traffic and leave a table, but new traffic passes through
  bypass; cleanup deletes only that session’s exact table.

Normal timing checks require every ping reply, using broad scheduler tolerances.
Overload and forced-crash checks explicitly allow the documented packet loss.
The complete run output can be saved with:

```sh
sh scripts/test-linux-network.sh > build/linux/network-test.log 2>&1
```

## Bug fixed by native testing

Stop previously deleted the nftables table before sending accept verdicts for
held packets. Native testing reproduced 100% loss of the single held test reply.
Cleanup now flushes queue rules while retaining base chains, drains the queue,
closes it, and only then deletes the table. The same regression test now receives
the held reply promptly. Mocked lifecycle expectations were updated, including
failure to flush rules and retry after failed final table removal.

The previous manual guide also used ordinary `ping`, whose kernel receive
timestamps hid inbound delay in this environment. The guide and automated checks
now use `ping -U` for delivery-to-userspace timing. TCP/UDP tests independently
measure elapsed time with a monotonic clock.

## Still unverified

Physical-network/game behavior, long-duration high-throughput load, fragmentation,
unusual IPv6 extension chains, and explicit checksum/GSO/offload stress remain
outside this suite. Partial setup and helper/removal failure injection are covered
by deterministic mocked tests, not every native fault condition. Testing one
Fedora kernel does not establish support for every distribution or kernel.

A subsequent [Qt integration](QT-LINUX.md) now connects the GUI, adds Linux
persistence, and separates privileged networking into a helper. Linux global
hotkeys, packaging, and physical desktop/game acceptance remain future work.
