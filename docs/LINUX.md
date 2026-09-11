# Linux command-line prototype

This connects the shared Clumsier controller and JSON preset format to a real
Linux NFQUEUE backend. It can start/stop capture, apply manual Lag, calculate
target-ping steps from your baseline, and navigate sequences while running.
The interface is a terminal, not the Windows GUI; there are no global hotkeys.

**Native build and isolated real-packet tests now pass on Fedora 42.** See
[the validation report](LINUX-VALIDATION.md) for versions, measurements, the
shutdown fix, and remaining limits. The terminal prototype is ready for local
experimentation. The [Qt interface is now connected](QT-LINUX.md);
physical-network/game acceptance remains.

## Quick start on this Linux desktop

Install the build dependency once (already done on the development machine):

```sh
sudo dnf install libnetfilter_queue-devel
```

From the repository root:

```sh
sh scripts/build-linux.sh
sh scripts/test-linux-network.sh --demo
```

The demo runs the actual Clumsier executable and sends real packets between two
private network namespaces connected by a virtual Ethernet cable. It displays
baseline RTT, the four sequence steps (about 150, 0, 100, and 50 ms), and RTT after
Stop. It needs no sudo and does not affect your Internet connection. Its
namespaces disappear when their processes exit; no named namespaces or host
firewall rules are created.

Run all automated checks with:

```sh
sh scripts/test-linux-logic.sh
sh scripts/test-prototype.sh
sh scripts/test-linux-network.sh
```

The packet suite takes roughly a minute. It requires Python 3, `unshare`,
`nsenter`, `setpriv`, `ip`, `ping`, and `nft`, plus enabled unprivileged user/network
namespaces and kernel NFQUEUE support. These are available on the tested Fedora
machine. If `unshare` is denied, use the sudo-based manual lab below on a system
that permits it; do not remove the runner's isolation checks.

**Use `ping -U` when measuring inbound delay.** Ordinary iputils ping can use a
kernel receive timestamp from before NFQUEUE held the reply and display almost
zero RTT even though delivery to the application was delayed. The runner uses
user-to-user timing and also checks TCP/UDP echoes with a monotonic userspace
clock.

WSL 2 is suitable for the first build and isolated namespace tests once NFQUEUE
and nftables support are available in its installed kernel. This exercises Linux
traffic inside WSL; it does not automatically affect a game running in Windows.
Use the laptop's Linux partition for later physical-network acceptance testing.

## Build and run

Required development tools are a C11 compiler, libc/kernel development headers,
pthreads, pkg-config and libnetfilter_queue development files. Runtime needs
nftables (`nft` in a standard system path), NFQUEUE kernel support and
CAP_NET_ADMIN privileges (normally sudo for this prototype). Scripts install
nothing. Debian/Ubuntu development package names commonly include
`build-essential`, `pkg-config` and `libnetfilter-queue-dev`; use your
distribution's corresponding packages. The isolated test also uses iproute2,
ping and sudo.

From the repository root on Linux:

```sh
sh scripts/test-linux-logic.sh
sh scripts/test-prototype.sh
sh scripts/build-linux.sh
./build/linux/clumsier --help
```

After the isolated test below passes, a normal invocation is:

```sh
sudo ./build/linux/clumsier --preset examples/four-leaps.json --baseline 50
```

The example selects all inbound IP traffic. Edit a copy's structured traffic
selection to limit it to one server. Loading never starts capture automatically.
Console commands include:

```text
status
start
next
previous
reset
stop
baseline 40
load examples/four-leaps.json
delay 500 0
start
delay 0 0
quit
```

`delay IN_MS OUT_MS` switches to manual values with the current traffic
selection. Target-ping steps use the same shared calculations as Windows.
Stop preserves your sequence step. Quit, EOF, Ctrl+C and SIGTERM request cleanup;
SIGKILL or a crash cannot execute cleanup.

Keep failed build output. Record `uname -a`, `cc --version`,
`pkg-config --modversion libnetfilter_queue` and `nft --version` with results.

## How it works

The constructor allocates coordination resources without changing networking.
Start creates a worker, binds a randomly chosen unused queue, then installs one
atomic nft transaction in a private `inet clumsier_<random token>` table.
Using CREATE prevents replacement of an existing table. The ownership comment
is checked before deletion. Queue collisions are retried; another consumer is
never unbound.

Structured selection covers Any/TCP/UDP, inbound/outbound/both, numeric IPv4/IPv6
remote addresses and TCP/UDP remote ports. Inbound matches remote source;
outbound matches remote destination. Addresses are validated with inet_pton and
canonicalized. Generated input is bounded and nft runs through an explicit
executable path with no shell. Native filters, hostnames and scoped-address
strings such as `fe80::1%eth0` are rejected rather than ignored.

Packets remain in the kernel. The worker stores notification IDs and returns
NF_ACCEPT with no replacement bytes. One worker owns the queue; validated
configuration changes are copied under a lock and wake it. Failed validation or
wakeup leaves prior settings intact. Every processing pass recomputes remaining
waits from current settings and original receipt times. Zero delay or disabled
directions release held packets too.

Stop flushes the private table’s rules while retaining its base chains, then
accepts held IDs and drains readable notifications, closes the queue, and finally
deletes the empty table. Deleting the table before draining unregisters the
Netfilter hooks and can discard queued packets; the packet suite covers this
regression. Repeated Stop is safe. Worker failures
mark capture stopped, report errors and follow the same cleanup path. Start joins
a previous worker and retries outstanding cleanup before another session.
Failed rule removal retains ownership for retry. Errors include the exact table
name if manual cleanup remains necessary; no global ruleset is flushed.

## Why NFQUEUE rather than netem?

The upstream [netem implementation](https://raw.githubusercontent.com/torvalds/linux/master/net/sched/sch_netem.c)
assigns time_to_send during enqueue and consults that saved value at dequeue.
Updating latency does not recalculate existing timestamps. Clumsier requires
changed delay to govern already-held packets, so this uses verdict scheduling.

The [libnetfilter_queue documentation](https://www.netfilter.org/projects/libnetfilter_queue/doxygen/html/group__Queue.html)
describes metadata delivery, verdicts, capacity, fail-open and GSO flags.
Unchanged verdict payloads preserve kernel checksum/offload data.
The [nftables manual](https://www.netfilter.org/projects/nftables/manpage.html)
describes hooks, queue verdicts and meta l4proto, which follows IPv6 extension
headers. NF_ACCEPT resumes kernel processing; no new packet or NF_REPEAT is
used, avoiding a reinjection loop.

## Limits and failure behavior

- Time starts at userspace notification receipt rather than kernel enqueue.
  Netlink backlog can add unintended delay; measure this under load.
- Capacity is 1,024 IDs. Overload may pass traffic without requested delay.
  Kernel fail-open and rule bypass do not guarantee lossless overload, recovery
  of held packets after a crash, or protection against a hung live consumer.
- Verdict sends have a one-second socket timeout. nft helpers have a bounded
  wait; ambiguous installation failure checks ownership before cleanup.
  Lost notifications or a broken socket can make held packets unrecoverable.
  Closing may drop those packets; the backend reports these errors.
- A crash can leave a private bypass rule. New packets pass without its queue
  listener, but remove the stale rule after inspecting the exact table reported.
  Never use `flush ruleset` for cleanup. Concurrent administrator edits are
  outside the prototype's ownership guarantee.
- Capture is local input/output, not forwarding or bridging. Loopback may
  cross both directions and incur both delays.
- Packet bytes, fragments and offload metadata remain in the kernel, but port
  matching on fragments and unusual IPv6 extension chains need native tests.
  The prototype does not force-load defragmentation modules.
- The high-level libnetfilter_queue API is documented as deprecated. It keeps
  this prototype readable; migration to the lower-level libmnl API can follow
  native verification. The Qt interface now uses a separate privileged helper;
  the terminal prototype still runs directly with network privileges.

## Isolated end-to-end test

Use fresh namespaces without a route to the real network. First inspect
`ip netns list`; if either name exists, use different names consistently.
Do not reuse or delete another namespace. Complete each setup command before
continuing:

```sh
sudo ip netns add clumsier-client
sudo ip netns add clumsier-server
sudo ip -n clumsier-client link add c0 type veth peer name s0 netns clumsier-server
sudo ip -n clumsier-client addr add 192.0.2.1/24 dev c0
sudo ip -n clumsier-server addr add 192.0.2.2/24 dev s0
sudo ip -n clumsier-client link set lo up
sudo ip -n clumsier-server link set lo up
sudo ip -n clumsier-client link set c0 up
sudo ip -n clumsier-server link set s0 up
sudo ip netns exec clumsier-client ping -U -c 5 192.0.2.2
```

In terminal A, from this checkout:

```sh
sudo ip netns exec clumsier-client ./build/linux/clumsier --preset examples/four-leaps.json --baseline 50
```

In terminal B:

```sh
sudo ip netns exec clumsier-client ping -U -i 0.2 192.0.2.2
```

**No manual firewall setup is needed.** Enter `start`, then `next` three times.
The example adds about 150, 0, 100 and 50 ms respectively. The physical lab
baseline is near zero; the supplied baseline of 50 is deliberately used to test
the shared calculation. Actual lab RTTs therefore approach the added delays,
not the example's absolute ping targets. Use individual RTTs, not only averages.

Check:

1. Start twice and Stop twice remain responsive. After Stop, pings return to
   baseline and `sudo ip netns exec clumsier-client nft list tables` no longer
   lists this run's private table. Repeat 20 cycles.
2. While running, enter `delay 5000 0`, allow packets to enter for a second,
   then `delay 0 0`. Held replies should arrive promptly instead of waiting
   five seconds. Stop with held traffic should also release it.
3. Enter `delay 2000 0`; within half a second enter `delay 5000 0`.
   A held packet should wait roughly five seconds from original receipt,
   not two seconds or five seconds from the edit.
4. Stop and reload the sequence. Check Next/Previous/Reset, end behavior,
   baseline edits and no automatic Start. Invalid values and unsupported native
   filters must leave the running configuration intact.
5. Test a copied preset restricted to 192.0.2.2, add a second address on server
   s0, and verify traffic to that second address stays at baseline. Repeat
   TCP/UDP remote-port selection against a local server/echo tool; another port
   and the other protocol must not gain delay.
6. Start without a preset, or load one capturing both directions, and try
   `delay 100 150`: expect about 250 ms added RTT. Separately test real
   inbound-only and outbound-only capture selection.
7. Add 2001:db8:1::1/64 to client c0 and 2001:db8:1::2/64 to server s0, wait for
   address readiness and verify ping -U -6. Repeat with the numeric remote IPv6
   address in a preset. IPv6/fragment/offload verification remains pending.
8. Start without privileges: it should fail clearly with no private table.
   Also test malformed addresses and Windows native filters. Two instances in
   the lab should own independent queues/tables; stopping one must preserve the
   other's resources. Active delays from both instances can add together.
9. Quit, EOF, Ctrl+C and SIGTERM should remove the table. Test forced crashes only
   inside the lab; held-packet loss is possible, but new traffic should recover
   through bypass. Inspect/remove that run's specific stale table afterwards.

Stop ping, quit the app and confirm all test processes have exited. Remove only
the namespaces you created:

```sh
sudo ip netns del clumsier-client
sudo ip netns del clumsier-server
```

If setup failed partway, undo only successful steps. Verify normal laptop
networking still works. Return build output, versions, RTT changes, errors and
cleanup observations.

## Tests and source map

lag_queue tests live edits, zero/disabled directions, capacity, exact deadlines,
failed verdict ownership/retry and 64-bit time. rules_session tests IPv4/IPv6
canonicalization, protocol/direction/port predicates, injection rejection,
ownership-aware cleanup ordering, partial setup failure, removal/drain failures,
and repeated restart. Both pass strict native Linux GCC C11 checks as well as the earlier Windows checks; rules_session
links ws2_32 on Windows for numeric IP parsing. The automated network suite additionally exercises native socket/process/thread
boundaries with actual traffic. See the validation report for remaining cases.

backend.c owns the worker and NFQUEUE boundary; nft.c owns helper execution and
ownership queries; rules.c generates filters; session.c enforces setup/teardown
ordering. src/platform/linux/main.c connects this to the shared console and
controller. Windows sources are unchanged.

The earlier nfqueue_probe.c and build-linux-probe.sh remain optional low-level
diagnostics requiring a named isolated namespace and manual rules. Use
build-linux.sh for the complete application and acceptance testing.
