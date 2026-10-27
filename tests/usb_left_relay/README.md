# USB Left relay host tests

Run from the repository root on Linux/WSL with Python 3, a C compiler and sanitizer runtime:

```bash
bash tests/usb_left_relay/run.sh
```

The script compiles temporary C11 executables with warnings as errors,
AddressSanitizer and UndefinedBehaviorSanitizer, runs them and removes them.
It does not require Zephyr or a keyboard.

The wake regressions are part of `run.sh`. `test_wake.c` uses the actual
protocol and both relay roles, with mocked inter-half delivery and USB hooks.
It checks selected/armed idle suspension for 15 seconds, outstanding transfers
and delayed drains, passive renegotiation, one request per fresh intent, a new
request after failed wake, rejected/expired sends, ordinary cleanup revisions,
neutral-first resume, three sleep cycles, stale boot/challenge/USB generation,
permission changes, duplicate packets and rapid suspend/resume/suspend before
a new relay session begins. Old-schema peers are rejected.

`test_wake_input.c` compiles the actual report-sender entry point, active-report
updater, action-state updater, debounce/commit functions, action dispatcher,
held-source capture and wake helpers, together with the real route coordinator.
The test mocks scheduler/power, postponer queuing/replay, report construction and
the USB adapter; it does not replace the blocked-route branch or wake decisions.
It checks keyboard/media/modifier and mouse-button actions on all four physical
source slots, releases/held input, quarantine, postponed input from before
suspend, link/session invalidation, synthetic pointer keys, motion/scroll,
neutral input, unselected/unarmed hosts, pairing/lock/test/suppression modes,
explicit selection/reselection, held connection actions and a switch away later
in the same input pass. Input still runs while normal reports are discarded.
The coordinator test also verifies automatic selection/config replacement and
retained sleeping selection remain quiet. The integration harness disables only
unused-parameter warnings for extracted production callbacks and platform mocks;
all other warnings remain errors, with ASan/UBSan enabled.

Run the negative controls against the original implementation on Linux/WSL:

```bash
python3 tests/usb_left_relay/run_wake_baseline.py 82b2a7167
```

The runner extracts only the four unchanged relay/protocol sources into a
temporary directory. Both regressions must reach their assertion failure:
`idle suspend: wake_calls=1` and `rapid sleep replay: extra_wake_calls=1`.
The fixed suite requires zero in both cases. This establishes that the tests
detect the original idle-wake bug and the need for a sleep-cycle packet fence;
it does not establish physical Windows timing.

## Wake policy and paired-update contract

Retaining USB Left selection never requests wake. A fresh debounced physical
key whose resolved action emits keyboard/media/system/modifier input or a mouse
button may queue one wake while that selected host is suspended and authorized
for remote wake. The updater records source freshness before postponement,
consumes it when the action activates, and rejects old/held/synthetic sources.
Mouse motion/scroll, report retries, neutral cleanup, macro-generated reports,
and layer-only actions do not create input wake intent. Explicit host selection
also requests wake, including reselecting the sleeping current host. Automatic
selection/config reload does not. Held connection actions honor quarantine.

Wake intent is invalidated on deselection, permission/configuration/suspend
transitions, USB generation or peer/link replacement, and peer staleness.
It expires after 128 ms while waiting for cleanup; each accepted input gives
one send attempt, so a new press/selection can retry without background wake
retries. An already transmitted packet has the USB state revision in its existing
`selection` field. The left rejects it after the state changes, and advances the
revision when accepting it to reject duplicates. Boot/challenge/generation alone
cannot distinguish rapid sleep cycles before a new BEGIN completes.

The revised meaning of `Wake.selection` changes the schema from `0x01DD041D`
to `0x01DD041E`. **Update both halves together.** Older peers intentionally fail
negotiation; packet sizes and the user-configuration format are unchanged.
The same transport-independent relay applies over the UART bridge and BLE.
Physical sleep/wake acceptance on both links remains required.

The firmware version contract has a separate compiler-based check:

```bash
python3 tests/usb_left_relay/test_versions.py
```

It runs the real version generator and compiles one generated source under 36
target/relay/zero-version combinations. Only relay-enabled UHK80 halves may use
`UHK_USB_LEFT_RELAY_FIRMWARE_VERSION`; disabled builds, other devices and modules
retain the ordinary version. Protocol/configuration versions and the zero-version
checksum pass are also checked. This does not query a physical keyboard.

`test_relay.c` exercises the real protocol codecs and both relay roles through
deterministic fake UART/USB endpoints: malformed/truncated messages and codec
fuzzing; lost reports/acks; immutable retries; deduplication of relative input;
stale tokens, sequences, USB generations and state revisions; neutralization;
leases; sleeping-host cleanup; deadlines; and peer reboot recovery. It also
checks closure of a lost BEGIN and rejection of delayed activation, and retains
close acknowledgements across sleeping-host neutralization on resume. It
checks transfer completion identity, queued ownership, cancellation failures
and suppression of independent physical sources with the same HID usage.

`test_host_route.c` compiles the actual coordinator with mocked platform hooks.
It covers drain/release/preparation ordering, held-source gating, offline
selection and recovery, preparation timeout, request coalescing, generation
changes, indefinitely unproven drains, deferred sleeping-host neutral reports
and configuration replacement/removal of the selected slot.
The actual `switchHost` command is compiled with relay enabled and disabled:
left handovers yield while pending; right/BLE, active same-host and invalid-name
selections retain synchronous behavior. Dry-run selection has no side effects.
It also compiles the actual USB-generation observation block from the UHK
adapter and checks that reconnect/protocol replacement drains old transfers and
confirms all-up delivery before reopening input. A delayed all-up completion
cannot be bypassed by elapsed time, and the drain is not rerun over pending
neutral reports.

With the pinned and patched west SDK present, also run:

```bash
bash tests/usb_left_relay/run_c2usb_completion.sh
```

This C++20 sanitizer test compiles the actual c2usb `process_ep_event` method
and transfer class, with only event/platform plumbing mocked. It covers Nordic
IN buffers consumed to zero remaining bytes, original buffer identity and
submitted length, failed/cancelled transfers, and OUT receive lengths. It also
passes those completions through the actual relay transfer-identity helper.
Before the c2usb completion patch, the consumed IN case fails; after the patch,
successful sends retire their tickets while cancellations still fail delivery.

The timer integration regression needs Python 3 and the same C sanitizer runtime:

```bash
bash tests/usb_left_relay/run_relay_poll.sh
```

It compiles the actual relay poll event handler, reuses the existing protocol
platform fakes, and models the scheduler's rule that a later request cannot
postpone an already-due timer. With no external wakeups, both relay roles must
remain active for five lease periods. Before rearming the popped event in its
handler, the left lease expires; with the rearm, heartbeats maintain the session.

The normal keyboard completion regression also needs a C++20 compiler:

```bash
bash tests/usb_left_relay/run_normal_keyboard_completion.sh
```

It compiles the actual adapter queue, tracked submission and completion methods
with mocked platform hooks. It reproduces a key-down completion arriving after
the semaphore times out and the desired report is rebuilt as key-up. The delivered
baseline must retain the immutable submitted state, so the release remains a
change requiring delivery. It covers completion ownership before main-thread
drain, cancelled reports, stale route/USB generations and selected BLE delivery.
The original completion path fails this regression.

The relay completion regression is included in `run.sh` and can also run alone:

```bash
bash tests/usb_left_relay/run_relay_completion.sh
python3 tests/usb_left_relay/run_relay_completion_baseline.py 07666ac93
```

It compiles the actual report sender/retry policy, semaphore, report comparisons, canonical-buffer switches and UHK relay delivery callback with the complete relay protocol/state machines. GPIO/report construction, scheduler, recorder and USB/inter-half timing are mocked. A sequence of failing sends reaches the sender's give-up state; a later accepted relay report has its legacy in-flight latch cleared by the real sender. The test waits behind the real pending-report gate, drops an acknowledgement to exercise immutable retries, then delivers the matching DONE and rebuilds an all-up report. Enter and left GUI releases must reach the host, clear the give-up state and advance the delivered baseline; mouse buttons and media controls must recover too. Duplicate, old-sequence and wrong-token DONE packets cannot acknowledge a newer report or advance its baseline twice. No pending report buffer is artificially overwritten. The baseline runner requires the original implementation to reach `host_released=0 given_up=1 confirmations=0`, reproducing the October 27 report mismatch. These transport-independent checks do not establish physical Bluetooth timing or replace paired hardware acceptance.

The upstream-merge wake regression (`run_transport_wake.sh`, also included in `run.sh`) compiles the production wake helper and the right-USB keyboard/mouse/control switch arms with relay enabled and disabled. It verifies that a successful wake releases the matching semaphore without submitting the waking report, awake sends submit exactly once through the configured path, submission errors propagate, a missing session stays disconnected, and mouse movement alone does not request wake. USB/session/queue and semaphore boundaries are mocked; this does not measure physical wake or completion timing.

The wireless integration regression compiles both UHK endpoint roles:

```bash
bash tests/usb_left_relay/run_wireless.sh
```

It uses the actual transport selector, transmit queue/worker, ingress guards,
link-generation observation and Messenger dispatch with the real portable
protocol. Mock radio delivery adds 20 ms in each direction. Coverage includes
wireless-only boot and idle leases, bridge preference, inactive BLE changes,
queued old-route reports, blocked USB drains, fresh tokens during fallback and
bridge restoration, reconnect generations, wrong peers, loss of both links,
key-up delivery, and rejection of unencrypted/undersized ATT connections. The
left NUS notification test fails if relay messages broadcast instead of targeting
the paired right half. These fakes do not establish real radio timing or pairing.
It also covers transient and perpetual Bluetooth-slot contention, the transmit
worker's 32 ms retry bound, and route loss while a retry owns its immutable bytes.

The Bluetooth send-contention regression uses the actual NUS send methods:

```bash
bash tests/usb_left_relay/run_nus_contention.sh
```

It checks nonblocking relay acquisition, bounded failure for legacy callers,
ownership of semaphore slots on busy/error/oversize paths, and persistent client
payload storage while an asynchronous GATT write is pending. Stack-use-after-return
detection is enabled. Relay transmit work runs on a dedicated queue so transport
allocation/waits cannot block Bluetooth completion work on the system queue.

```bash
bash tests/usb_left_relay/run_wireless_latency.sh
```

This compiles the actual Bluetooth profile selection/application methods. It
checks the selected awake wireless left host requests interval 6 (7.5 ms) with
zero peripheral latency, unchanged selections do not renegotiate, bridge or
suspended-host transitions restore NUS defaults, and unrelated host profiles
retain their existing rules. Actual negotiated timing still needs measurement.

USB enumeration/power lifecycle experiments and their regressions are preserved
on the separate `codex/uhk80-usb-c-reconnect` investigation branch. The feature
suite above retains USB-generation draining, key release, and bridge/Bluetooth
handover coverage.

These tests do not execute the full Zephyr/c2usb C++ adapter, Nordic UDC driver,
physical key scanner or native Windows updater. Firmware builds check adapter
integration; the hardware matrix in the
[development guide](../../doc-dev/technical/uhk80-dual-usb-hosts-development.md)
is required to establish runtime behavior and performance.
