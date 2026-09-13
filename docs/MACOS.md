# macOS Lag prototype

The intended end-to-end prototype includes an AppKit host, Network Extension
packet filter, shared JSON preset loading, manual baseline entry, and Start /
Stop / Previous / Reset / Next controls. It is separate from the Windows UI and
supports Lag only. No delay starts automatically.

**Validation:** the portable scheduler and synthetic packet-selection tests pass
on macOS 15.7.3 (arm64), using macOS SDK 26.0. The Apple API probe and both the
AppKit host and packet-provider executables compile and link with the existing
strict warning flags. The shared prototype console tests also pass. These checks
were run on September 13, 2026; they do not install or activate an extension.
Signing, activation and real packet delivery remain unverified: the validation
machine has no valid code-signing identity. This is not a tested macOS release.

## Build and run

Use macOS 13+ and Xcode command line tools. The minimum is 13 because both peers
use the public [NSXPC signing-requirement API](https://developer.apple.com/documentation/foundation/nsxpcconnection/setcodesigningrequirement(_:)).

For an initial compile/link check with **no Apple account or signing profiles**,
run `sh scripts/build-macos-native.sh`. This creates standalone executables only;
they cannot activate the provider without the signed bundle described below.

Register a host bundle identifier in your Apple Developer account, plus another
with `.packet-filter` appended. Enable Network Extensions and the matching app
group for both. The build uses `TEAM_ID.BUNDLE_ID` as the group and appends
`.control` for its Mach service. The host also needs the System Extension
installation capability. Prepare Developer ID provisioning profiles for both
identifiers with these capabilities. Their application identifiers must use the
same team prefix as `TEAM_ID`.

From the repository root, substitute your real values:

```sh
export TEAM_ID='YOUR_ACTUAL_TEAM_ID'
export BUNDLE_ID='YOUR_REGISTERED_HOST_BUNDLE_ID'
export SIGN_IDENTITY='YOUR_DEVELOPER_ID_APPLICATION_SIGNING_IDENTITY'
export HOST_PROFILE='/absolute/path/to/host.provisionprofile'
export EXTENSION_PROFILE='/absolute/path/to/extension.provisionprofile'
sh scripts/test-macos.sh
sh scripts/build-macos-app.sh
```

The script compiles and links both executables, generates metadata/entitlements,
embeds your profiles, signs the nested extension then app, and verifies signatures.
Output is `build/macos/Clumsier.app`. Nothing is installed or activated. A valid
signature does not prove that a profile authorizes every requested feature.

For sharing, notarize the signed app using your configured Apple credentials:

```sh
ditto -c -k --keepParent build/macos/Clumsier.app build/macos/Clumsier.zip
xcrun notarytool submit build/macos/Clumsier.zip --keychain-profile 'YOUR_PROFILE' --wait
xcrun stapler staple build/macos/Clumsier.app
```

Copy the signed app into `/Applications` before activation. Do not disable system
protections to work around signing problems. Launch it, then:

1. **Enable provider.** Approve the system extension and network filter in System
   Settings when requested. An enabled provider initially allows traffic; it is
   different from a running delay session.
2. **Import sequence.** Select a shared Clumsier JSON file, such as
   `examples/four-leaps.json`. Windows-native filter strings are rejected.
3. Enter your normal server ping for target-ping mode. The field deliberately
   starts blank. Added-delay mode may leave it blank.
4. **Start**, then **Next**, **Previous**, or **Reset**. The cursor changes after
   the provider accepts the setting. Navigation while stopped never starts delay.
   The preset's `loop` setting controls wrapping.
5. **Stop** releases queued packets and leaves the installed provider allowing
   traffic. **Remove provider** removes this app's filter preferences and requests
   deactivation. The app reports when macOS requires a restart.

Activation can take time. If the initial command cannot connect, retry Stop after
activation finishes, then Start. For signing errors, check the build output and
macOS Console logs for Clumsier rather than assuming compilation proves activation.

## Implementation

`src/platform/macos/host.m` owns the minimal UI and asynchronous cursor changes.
Both host and provider reuse the shared `presetParse`, `presetValidate` and
`presetResolve`: no duplicate file schema or target-ping arithmetic. The host
does not force asynchronous activation/XPC into the synchronous `AppController`
interface, which would block the UI. Its cursor bridge commits acknowledged
transitions only.

`control.h` sends bounded JSON, baseline and step messages over NSXPC. Both peers
require the configured signing team and exact peer bundle identifier. One host
connection controls a provider; requests must belong to that current connection.
Errors/timeouts report uncertainty instead of pretending unacknowledged changes
succeeded. The host sends heartbeats every three seconds; the provider flushes
on disconnect or stops after roughly ten seconds without heartbeats.

`provider.m` owns the filter, timer and retained packet objects. The native delay
call executes on the actual packet callback thread under a short state lock.
Timer/control operations share that lock. Actual allow calls run after unlocking;
a dispatch group tracks detached batches so Stop/provider-shutdown replies wait
for outstanding allow calls. Closing the host invalidates its connection and
leaves the extension installed. Provider crashes can lose retained packets;
orderly disconnect cleanup is not a crash-delivery guarantee.

`held_packets.c` preserves arrival timestamps and revisits current settings on
every pass. Zero/disabling a direction releases its queue. Directions are scanned
independently. Capacity is 256 packets and 4 MiB of packet lengths; excess arrivals
pass immediately and can reorder under load. The timer requests two-millisecond
ticks with one-millisecond leeway; actual timing depends on macOS and load.
Invalid settings preserve accepted state. No privileged shell command is built
from preset values.

## Traffic coverage and limits

`packet_match.c` handles Ethernet, up to two VLAN headers, IPv4/IPv6 and bounded
IPv6 extension-header chains. Numeric remote IP, TCP/UDP/Any, capture direction
and remote ports are supported. Remote means source inbound and destination
outbound. Packet bytes and checksums are unchanged; the framework resumes the
same retained object.

Unknown link formats, truncated/malformed frames, IPv6 jumbograms and excessively
long extension chains pass without delay. Port-filtered fragmented datagrams
also pass: there is no fragment-association cache. Fragments with another
extension header following the fragment header cannot be classified as TCP/UDP
and pass protocol-specific filters. These are coverage limits, never reasons to
broaden a user's filter. Loopback, tunnel interfaces, offload and VPN coexistence
need native observation. Start tests with ordinary Ethernet/Wi-Fi peer traffic;
avoid fragmented datagrams when selecting ports.

There is no macOS preset editor, persisted library, global hotkeys, wheel control,
timed playback or UI parity. Importing shared sequences and operating their steps
is this prototype's scope.

## Tests and native acceptance

`sh scripts/test-macos.sh` runs portable queue and synthetic Ethernet tests.
Windows verification used GCC C11 with `-Wall -Wextra -Werror -pedantic`, plus
`-lws2_32` for address parsing. Coverage includes original timestamps, delay
increase/decrease, exact release boundaries, zero, direction disable, invalid
settings, stop/restart, limits, truncated frames, IPs, VLAN, IPv6 headers/fragments.
The small `build-macos-probe.sh` compiles API references only.
`build-macos-native.sh` also compiles and links the host and provider; both checks
pass on the Mac recorded above. Signed bundle activation and packet acceptance
are the next native checks.

After building/signing, record macOS/SDK version, CPU architecture and interface:

- Enable/import/start; measure a controlled peer's echo/packet timings, not just
  a game's smoothed ping. Verify TCP/UDP, IPv4/IPv6 and both directions.
- Confirm unrelated peers/ports remain unaffected and unsupported frames follow
  the documented bypass behavior.
- Hold packets, lower delay to zero and confirm prompt release. Increase delay
  and verify release against original arrival timestamps.
- Repeat Start/Start, Stop/Stop, Previous/Reset/Next, navigation while stopped,
  wrapping/nonwrapping ends, below-baseline targets and Lowest available.
- Stop/remove with a nonempty queue. Native allow calls must finish before
  shutdown; later sessions must contain no stale packets.
- Deny activation, supply bad profiles, interrupt XPC, close/pause the host and
  try an unrelated signed client. Errors must be visible and no unacknowledged
  transition should appear accepted.
- Force-terminate the provider and document retained-packet loss and future
  traffic recovery. Exercise sleep/wake, interface changes, load and a VPN before
  everyday use. Never reset global firewall rules as a cleanup shortcut.

## Primary references

Apple's [packet provider](https://developer.apple.com/documentation/networkextension/nefilterpacketprovider)
supports delay. Its [delay method](https://developer.apple.com/documentation/networkextension/nefilterpacketprovider/delaycurrentpacket(_:))
requires an active handler and delay verdict; releasing a held packet without
allowing drops it. The [allow method](https://developer.apple.com/documentation/networkextension/nefilterpacketprovider/allow(_:))
resumes delivery. These APIs motivated the retained queue.

[TN3134](https://developer.apple.com/documentation/technotes/tn3134-network-extension-provider-deployment)
documents system-extension/Developer ID deployment. The
[entitlement reference](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.networking.networkextension)
describes signing capabilities. Mach-service wiring follows the public design of
Apple's [Filtering Network Traffic sample](https://developer.apple.com/documentation/networkextension/filtering-network-traffic),
including app-group prefix and ordinary NSXPC lookup options. No Apple sample
source is bundled in this app.

[TN3120](https://developer.apple.com/documentation/technotes/tn3120-expected-use-cases-for-network-extension-packet-tunnel-providers)
directs local filtering toward filter providers rather than a pretend VPN. We
use neither that workaround nor PF rules. Review Apple's
[current Developer Program terms](https://developer.apple.com/support/terms/apple-developer-program-license-agreement/)
before distribution; this project's account entitlements/distribution eligibility
have not been independently verified.
