# UHK80 dual USB: implementation and development

The experimental firmware routes the complete keyboard to either half's USB-C
port. The right half runs keymaps, layers and macros; the left encodes and submits
relayed keyboard, mouse and consumer/system-control reports to its own computer.
Both USB command interfaces remain available while input selection changes.

This implements the [technical design](uhk80-dual-usb-hosts-design.md) behind
`CONFIG_UHK_USB_LEFT_RELAY`, which defaults to **off**. The experimental presets
enable it on both halves and enable a temporary host slot on the right. Standard
presets retain the existing feature selection. No Agent, descriptor, VID/PID,
configuration-version or bootloader change is included.

**Hardware acceptance is pending.** Cloud checks compile the actual firmware
and exercise the portable protocol and route coordinator; they cannot establish
USB cancellation timing, wakeup behavior, runtime stack headroom or two-computer
operation. The original cloud implementation did not flash a keyboard.

Initial local Windows testing on 2026-10-09 flashed both halves, established
UART relay negotiation, and exposed blocked switching and repeated keys. The
completion boundary had reported the Nordic driver's consumed IN buffer
(`data` advanced, `len` zero) as the sent report. This made successful delivery
look like failure to the relay adapter. The repository's second c2usb patch
preserves the submitted IN buffer and length; OUT callbacks retain received
lengths, and errors/cancellations still report zero transferred bytes. The
SDK-backed completion regression fails before that patch and passes after it.
Fresh firmware builds and signed-image verification pass with the patch. Both
corrected applications were flashed, and a control-path check activated left
USB and returned to right USB without leaving the route blocked. Relay
rejection/lease counters increased during that check; physical key-release
testing and the complete hardware acceptance matrix remain pending.

The user reported improved typing through right USB after that correction.
Left USB still expired its idle lease. Its poll event was popped after the main
loop's next-poll request had been ignored by the scheduler's earlier-deadline
rule, leaving no relay wakeup queued. The relay poll event handler now rearms
the next poll. An integration regression using that actual handler reproduces
lease expiry before the fix and maintains an idle session for five lease periods
after it. The completion, relay and coordinator tests and both signed builds
pass with both corrections. On hardware, corrected left USB stayed active for
20 seconds without vendor commands, with zero faults, retries or rejected
packets. The user reported that typing seemed to work through left USB, and
returning to right USB completed without faults. Two-computer input isolation,
held-key switching and the complete acceptance matrix remain pending.

A subsequent direct USB-C test on the right half produced repeating after
physical release; resetting that half made the same Mac/cable connection work.
The post-reset capture does not retain the failed transfer state. Code review
and an integration regression identified that a local USB-generation change
cleared the report baseline without requiring host-side all-up delivery. The
coordinator now holds input behind a drain and all-up completion barrier for an
awake, selected right USB session. The regression fails before this change and
passes after it. This correction is a candidate for the reported reconnect issue;
that Mac hot-plug scenario still needs a hardware retest.

A later Windows failure was captured before resetting: the physical/logical
keys were released, but the last completed right USB keyboard report still
asserted Backspace while both canonical comparison buffers were empty. A
filtered Windows Raw Input capture attributed down events without release to
the right USB interface. Successful normal keyboard completions could be
discarded after the semaphore timed out, or advance the baseline using a
working report already rebuilt after release. Normal USB/BLE submissions now
retain an immutable canonical keyboard snapshot; completion records that
delivered state even after timeout, leaving key-up distinct and requiring
delivery. An undrained keyboard completion prevents a new submission from
replacing its snapshot. Cancelled and stale route/session completions remain
excluded. The actual adapter queue/completion regression fails before this
correction and passes after it.

Both experimental release builds and signed-image checks passed. The corrected
right application was flashed, and the user confirmed key release from both
halves, including Backspace, after flashing and after a cable-only Windows USB
reconnect without reset. The existing next-connection key also successfully
selected the ready left USB host. These checks establish those scenarios;
direct Mac USB-C reconnect behavior, held-key switching and the remaining
acceptance matrix still need coverage.

Local Bluetooth fallback testing updated both halves, detached the bridge, and
selected left USB with both host ports on Windows. Initial testing exposed NUS
send contention and then a typing backlog. The relay now transmits from a
dedicated worker, uses strict/nonblocking send-slot ownership and persistent
client payload storage, and retries transient contention for at most 32 ms.
An active wireless left host also requests a zero-skip Bluetooth profile. All
regressions and both signed builds pass. Windows Raw Input confirmed matched
press/release events from the left USB interface. After the final uploads, the
user confirmed sustained cable-free typing, working key release and removal of
the backlog. The final route was active and unblocked with no current fault
reason, but the counters recorded one additional recovered fault/activation
during the latest test interval. Its trigger was not retained. Physical bridge handover, separate-computer Mac
testing and the remaining acceptance matrix still require coverage.

A subsequent USB reconnect with the bridge attached left Windows seeing only
right USB, while the bridge relay itself remained negotiated. Left reset with
that wiring did not enumerate; detaching the bridge and resetting left restored
its USB interface. A definite startup defect classified Nordic regulator READY
as disconnected. Only DISCONNECTED now maps to USB off; CONNECTED and READY
permit initial attachment. The initializer plus actual SDK start/attach regression
fails before that correction and passes after it. Both corrected images were
flashed and both USB interfaces returned in the bridge-detached setup. The
bridge-attached startup was subsequently confirmed; cable-only reconnect still
failed in the sequence below.

The corrected startup was subsequently confirmed with the bridge attached, but
the original warm sequence (remove bridge, unplug left USB, restore bridge,
restore left USB) still failed. Relayed charger state indicated USB power present
while Windows did not enumerate left USB. The USB worker now reconciles the
observed Nordic VBUS level with a stale MAC off/on state through the third c2usb
patch's normal power-event handling. Checks run at most every 100 ms and only
after the preceding local USB generation's cancellation/completion barrier
finishes. Inactive devices remain closed. That correction passed the
SDK/event/barrier regression but still failed the exact warm hardware sequence.
The Nordic controller patch also handles READY preceding the stack's deferred
enable: an inactive peripheral is not started in the power callback, while
enable starts an already-ready regulator without requiring another edge. The
actual driver ordering regression fails before the patch and passes afterward.

Warm reconnect still fails in the original physical sequence. Earlier diagnostics
misinterpreted USBPULLUP=0 while LOWPOWER=1 as a disconnected pull-up. The
[nRF52840 product specification, section 6.35.12](https://devzone.nordicsemi.com/cfs-file/__key/communityserver-discussions-components-files/4/nRF52840_5F00_PS_5F00_v1.8.pdf)
states that USBPULLUP is inaccessible during low power. The resulting periodic
controller restart logic has been removed, as have the speculative suspend-event
changes. Power reconciliation uses only the observed VBUS level; an unchanged,
suspended controller is not repeatedly detached.

The fourth Nordic SDK patch restores normal register access within a fresh
peripheral-enable sequence, before the pull-up is attached. It leaves a live
suspended connection alone. The actual peripheral-enable regression checks
retained LOWPOWER; the preceding driver without this correction fails it.
Temporary fifth-patch diagnostics count reset/setup/resume and power events,
with HFXO state reported through the bridge. The pull-up reading is marked 255
when inaccessible. Builds and isolated regressions do not establish cable
reconnect correctness; hardware acceptance remains unresolved.

The r8 candidate addresses a separately reproduced control-endpoint lifecycle
defect. The pinned Zephyr API makes c2usb own fixed EP0 SETUP, DATA and STATUS
buffers. Controller disable leaves the control queues intact, and reattach
previously treated any nonempty EP0 OUT queue as a SETUP buffer being available.
A retained STATUS with the SETUP buffer no longer queued makes the next request
remain cached by UDC; bus reset previously drained only EP0 IN and has the same
weakness when bridge power maintains VBUS. The fourth c2usb patch retires both
control queues and drains their completion events before reusing fixed buffers.
It rearms SETUP after powered reset without restarting the controller, preserving
a new request cached during retirement. Posting the retirement barrier uses a
nonblocking queue operation: if completions fill the event queue, the USB worker
continues consuming and retries at the queue tail after each event. Fixed buffers
remain retired until the successfully queued barrier is consumed. This avoids
blocking the worker while posting to its own queue; the full-queue regression
rejects the preceding r7 implementation. The sixth Zephyr patch clears cached
SETUP belonging to a disabled attachment while holding the UDC mutex. The
preceding SDK fails the retained-STATUS, powered-reset and obsolete-cache
regressions; the candidate passes all of them and both signed builds. This is
a tested software defect and a candidate explanation for the reconnect failure;
the original hardware sequence must still pass before attributing that failure
to it.

The original warm sequence still failed after r8 was flashed to both halves,
with both controller workers active and the left USB controller powered. r9
adds temporary bounded driver and control-stack traces of SETUP headers,
reply lengths, enqueue/completion/stall stages and retirement/cache flags.
Only the eight-byte USB control header and metadata are recorded; report and
control-transfer payloads are excluded. A worker snapshot reads queue/cache
state under the UDC mutex. After enumeration stops, the left main loop forwards
small batches through the bridge. These diagnostics change no reconnect policy.
They are intended to identify the transaction where enumeration stops before
choosing another correction.

## Investigation paused on 2026-10-09

The feature branch is saved as an experimental checkpoint. Wireless left-host
typing from both halves, key release, and removal of the typing backlog were
confirmed on Windows. The full acceptance matrix remains incomplete.

The outstanding failure is specific to this tested sequence with right USB
connected to Windows throughout: detach the bridge, unplug left host USB,
reattach the bridge, then reconnect left host USB directly using USB-C to USB-C.
Windows does not enumerate the left interface; selecting it shows rolling dots.
Cycling the bridge again restores enumeration. Changing only the left cable/path
to USB-C to USB-A also restores enumeration without resetting either half or
cycling the bridge. Reverting to direct USB-C after that can retain the old
configuration while suspended, displaying `(asleep)` despite no Windows device.

The corrected trace places the final failing attachment after the earlier
successful control requests: VBUS is present and SETUP is armed, followed by
suspend and a brief resume, with no new bus reset or SETUP request. The worker
and clock remain active. The USB-A comparison receives a reset and enumerates
without another controller enable/start. This narrows the failure to direct
USB-C attachment, but does not prove a cable, CC, power-isolation or PHY defect.
No connector-level voltage/CC measurement or public board schematic was available.

The endpoint-retirement and READY-ordering corrections have regression coverage,
but did not fix that physical sequence. The removed periodic restart and
speculative suspend-event work must not be reinstated based on USBPULLUP reads
in low power: those registers are inaccessible there. Current diagnostic patches
record control-header metadata and valid awake START/RESUME register snapshots;
they do not change recovery policy or capture keyboard-report payloads.

All host regression scripts listed below passed at the checkpoint. Both halves
were previously built and signature-verified; the last additional diagnostic
build was left-only. Before restoration, hardware ran left r10 (SHA256
`2ee8882f09746b20e4375a0441deb4556d0f27e80db0a8fac50c4cedf57e55b0`)
and right r8 (`c33f18fd9fa117d6bb20aee8bd34486a29db4e0a8f36b29e207234585e75f698`).
Diagnostic labels and embedded source tags are not substitutes for those hashes.

Local captures, signed images, configuration backups and helper tools remain in
the ignored `build/local-uhk80/` directory. Start with
`usb-type-c-power-r10-investigation.md`, `usb-left-enumeration-r9b-live.log`,
`usb-a-path-restored-r9b.log` and `wireless-fallback-validation.md`. The paused
investigation should resume from the direct USB-C attachment evidence, rather
than treating bridge power as proof that a computer is attached.

The requested rollback uses the official
[18.0.4 release](https://github.com/UltimateHackingKeyboard/firmware/releases/tag/v18.0.4),
whose archive SHA256 is
`74814e2bb6ed58b09dff8a020ee3c2f07ec0d6e115cf0c80cee7ac729f4dfde2`.
Only application firmware is restored; the existing bootloaders and saved user
configuration are retained. Restoration results are recorded locally alongside
the release metadata and upload logs.

## Behavior and limits

- Connect Computer A to right USB-C and Computer B to left USB-C. Both halves
  must run these experimental images and remain powered. The relay prefers the
  inter-half UART bridge. Without that bridge it uses the halves' existing paired
  Bluetooth NUS connection; no additional computer Bluetooth pairing is needed.
  Radio disablement or missing inter-half pairing prevents wireless fallback.
  Allow time for the existing Bluetooth manager to reconnect after bridge loss.
- Wireless selection requires a ready, encrypted inter-half connection with an
  ATT MTU of at least 71 bytes. Messages use an explicit peer connection, and
  left-side notifications target the paired right half rather than broadcasting.
  Switching transport or reconnecting the selected link invalidates queued traffic
  and renegotiates a fresh session. Left USB resumes only after transfer drain
  and neutral reports complete. An inactive BLE link changing while the bridge
  remains ready does not invalidate the wired relay.
- An awake, selected left USB host using Bluetooth requests a 7.5 ms connection
  interval with zero peripheral event skips. Other host/inter-half profiles
  retain their existing settings. Profile changes are cached per peer, and the
  relay loop refreshes selection after an asynchronous host commit or handover.
  This avoids the ordinary NUS profile's four-event skips backing up typing.
- The optional development slot is `USB_Left_Dev`, allocated only in an empty
  serialized host slot. It never overwrites a paired/reserved slot. An existing
  unique USB Left slot is reused. Duplicate slots or a full table prevent
  provisioning. The development slot lives in RAM and is recreated after
  configuration reload; it is not saved to the user's configuration.
- On a switch involving USB Left, stop old user transfers, send neutral reports
  to the old host, prepare the new left session if needed, then commit the route.
  A suspended old USB host retains a neutral-report debt for resume after its
  in-flight transfers have been proven drained.
- Keys and modifiers physically held at the switch are suppressed by their
  source positions until release and a fresh press. This includes the physical
  key that invokes switching. Internal layer/keymap state is preserved. Macro
  output waits at the bounded transition barrier; later macro output is fresh
  input. Reports produced while the selected host is unavailable are discarded.
- Report retries retain the exact bytes and sequence number. The left acknowledges
  delivery after a successful USB completion, not UART acceptance. Duplicate
  relative mouse reports are acknowledged without submitting the movement again.
- A missing drain/cancellation proof blocks the transition. Elapsed time does
  not grant permission to reuse transfer storage or open another destination.
  Restore the bridge/host and inspect status; `retry` restarts negotiation but
  does not bypass this protection.
- Local USB submissions and cancellation run on the c2usb worker. Queued
  reservations remain owned through submission. Lifecycle fences cancel old
  transfers and drain their callbacks before allowing buffer reuse. Relay
  transmission uses a bounded four-packet worker queue for either inter-half
  transport. Each queued packet owns its connection and ingress generation;
  stale packets are discarded rather than redirected to another link.
  A dedicated 2 KiB relay worker stack keeps send-side waits outside Bluetooth's
  system completion queue. Relay NUS sends return busy immediately rather than
  waiting for a slot. Other NUS callers retain a bounded 256 ms wait, but a failed
  acquisition returns an error without submitting or releasing an unowned slot.
  The NUS client's payload buffer remains owned until asynchronous completion.
  The dedicated relay worker retries transient send-slot/allocation failures
  for at most 32 ms, yielding for 2 ms between attempts and rechecking transport
  identity before each one. Further loss follows the protocol's normal retries
  and deadline. This prevents state-sync contention from discarding every
  attempt at the same transport phase.

Timing constants are provisional: report retry 32 ms, report deadline 128 ms,
heartbeat 200 ms, left lease 700 ms, state age limit 1,000 ms, and a 100 ms
macro/preparation barrier. A failed drain can outlast that barrier while scans
and switch-key processing continue. Measure healthy switching and report latency
on hardware before treating the PRD's performance targets as met.

## Build the experimental images

For a fresh Windows/WSL workspace, follow
[the setup instructions](uhk80-windows-wsl-setup.md) first. In this cloud
workspace, source `/workspace/.uhk-env/activate.sh` and enter the firmware repo.
For a local workspace, source its `activate-uhk80.sh` instead.

From the firmware repository root in WSL:

```bash
west build --sysbuild --build-dir device/build/uhk-80-right-usb-left device -- --preset uhk-80-right-usb-left
west build --sysbuild --build-dir device/build/uhk-80-left-usb-left device -- --preset uhk-80-left-usb-left
```

For logging/debug builds, use separate directories:

```bash
west build --sysbuild --build-dir device/build/uhk-80-right-usb-left-debug device -- --preset uhk-80-right-usb-left-debug
west build --sysbuild --build-dir device/build/uhk-80-left-usb-left-debug device -- --preset uhk-80-left-usb-left-debug
```

Incremental builds do not need another SDK update or patch operation:

```bash
west build --build-dir device/build/uhk-80-right-usb-left
west build --build-dir device/build/uhk-80-left-usb-left
```

Each directory contains:

| File | Use |
| --- | --- |
| `device/zephyr/zephyr.signed.bin` | Signed application image for USB updating that half. |
| `device/zephyr/zephyr.elf` | Symbols for that exact image; retain it for crash analysis. |
| `merged.hex` | Full programming image including MCUboot, for probe workflows. Do not pass it to the USB application updater. |

Verify the application signature against this workspace's build key:

```bash
python ../bootloader/mcuboot/scripts/imgtool.py verify \
  -k ../bootloader/mcuboot/root-ec-p256.pem \
  device/build/uhk-80-right-usb-left/device/zephyr/zephyr.signed.bin
```

Repeat for the left image. This checks the generated image and build key; it
does not establish which key your installed bootloader trusts. Use the existing
bootloader and matching application image for your unit.

## USB flashing from native Windows

Use native Windows tools for USB, with a separate Agent checkout; WSL's
`node_modules` cannot be reused because HID/serial dependencies contain native
code. The firmware pins Agent commit
`a10a251fc812df5011f706b0d150b12526da9b83`, whose engines require Node
`>=24.16.0 <25` and npm `>=11.13.0 <12`. In PowerShell:

```powershell
New-Item -ItemType Directory -Force C:\uhk-dev | Out-Null
Set-Location C:\uhk-dev
git clone https://github.com/UltimateHackingKeyboard/agent.git agent
Set-Location agent
git checkout a10a251fc812df5011f706b0d150b12526da9b83
node --version
npm.cmd --version
npm.cmd ci
npm.cmd run build
```

Export the experimental applications from WSL:

```bash
cp device/build/uhk-80-left-usb-left/device/zephyr/zephyr.signed.bin /mnt/c/uhk-dev/uhk-80-left.bin
cp device/build/uhk-80-right-usb-left/device/zephyr/zephyr.signed.bin /mnt/c/uhk-dev/uhk-80-right.bin
```

Save your configuration and keep a working firmware release and another
keyboard available for the first hardware test. This checkout's MCUboot build
uses a single application slot; normal USB updating depends on a working
application command interface. Confirm your unit's recovery procedure first.
Close desktop Agent, connect the target half directly to Windows, and run the
matching command from `C:\uhk-dev\agent`:

```powershell
.\node_modules\.bin\tsx.cmd .\packages\usb\update-device-firmware.ts --vid=14248 --pid=7 C:\uhk-dev\uhk-80-left.bin
.\node_modules\.bin\tsx.cmd .\packages\usb\update-device-firmware.ts --vid=14248 --pid=9 C:\uhk-dev\uhk-80-right.bin
```

These commands flash hardware. VID is `0x37A8`, left application PID `0x0007`,
right PID `0x0009`. The updater requests MCUboot, uploads over its serial USB
interface and resets the device. Disconnect other matching UHKs or use the
updater's `--serial-number` selector. Native Windows installation, flashing and
physical bootloader recovery have not been tested in this cloud environment.

## Test without changing Agent

No configuration serialization is required. With both experimental images,
the bridge connected and right USB attached to the Windows machine running
these commands, use the existing execute-macro interface:

```powershell
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft status'
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft select left'
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft select right'
```

The commands keep working through right USB even when keyboard input goes to
the left computer. Development controls are asynchronous: read status again
after a request to see the resulting state. `zephyr uhk usbLeft provision`
requests RAM provisioning; `zephyr uhk usbLeft retry` restarts negotiation.
When the newly allocated slot has its default name, this existing macro also
selects it:

```powershell
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'switchHost USB_Left_Dev'
```

An existing configured USB Left slot keeps its name. `switchHost nextActive`
and other existing selection commands can select the ready RAM slot. For a
physical switching-key test, use a compatible existing Agent to assign the
macro, then confirm the RAM slot was recreated after configuration application.
The development commands bypass the need for Agent UI support for adding a
left USB slot.

The pinned Agent declares configuration version 14/protocol 4.17 while this
firmware declares 15/4.20. The existing low-level update/execute-macro paths do
not serialize a configuration. Do not infer that the pinned GUI can safely
rewrite a version-15 configuration. Agent configuration/UI compatibility is a
separate rollout task from this firmware experiment.

With compatible Agent logging or its Zephyr shell, inspect:

```text
uhk usbLeft status
uhk connections
uhk uartStats
uhk threads
uhk log usbSink 1
```

Status includes negotiation, configuration, activity, quiescence, pending reports,
`transport=bridge|bluetooth|none`, explicit connection ID and link generation,
USB generation, rejected/duplicate/retry/fault counters, numeric fault reason,
current/requested host and input blocking. `provisionResult` is the connection
ID on success, `-ENOSPC` for a full table or `-EEXIST` for duplicate left slots.
Fault reason values are defined in `device/src/usb_left_relay_protocol.h`.

For crash symbols without a debug probe:

```bash
"$ZEPHYR_SDK_INSTALL_DIR/arm-zephyr-eabi/bin/arm-zephyr-eabi-addr2line" \
  -e device/build/uhk-80-right-usb-left-debug/device/zephyr/zephyr.elf -f -C 0xADDRESS
```

Use the ELF from the image actually flashed. USB logs and offline symbols work
without a probe; live breakpoints and single stepping generally require SWD.

## Checks and hardware acceptance

Run the host tests under WSL:

```bash
bash tests/usb_left_relay/run.sh
bash tests/usb_left_relay/run_c2usb_completion.sh
bash tests/usb_left_relay/run_relay_poll.sh
bash tests/usb_left_relay/run_normal_keyboard_completion.sh
bash tests/usb_left_relay/run_wireless.sh
bash tests/usb_left_relay/run_nus_contention.sh
bash tests/usb_left_relay/run_wireless_latency.sh
bash tests/usb_left_relay/run_usb_power_startup.sh
bash tests/usb_left_relay/run_nrf_usb_ready.sh
bash tests/usb_left_relay/run_usb_control_retirement.sh
```

See [test coverage and boundaries](../../tests/usb_left_relay/README.md).
Release and debug experimental builds for both halves, standard release builds
for both halves and the dongle, and signed-image verification were exercised
in the cloud. Cloud linker sizes before the local corrections were:

| Target | Flash / 900,608 bytes | RAM / 262,143 bytes |
| --- | --- | --- |
| Right release | 855,188 (94.96%) | 242,848 (92.64%) |
| Left release | 654,568 (72.68%) | 202,072 (77.08%) |
| Right debug | 870,976 (96.71%) | 242,720 (92.59%) |
| Left debug | 668,808 (74.26%) | 202,136 (77.11%) |

These report static allocations, not runtime stack/heap headroom. The right
half's flash budget leaves little room for further debug instrumentation.
Hardware acceptance still needs:

1. Verify two-computer enumeration, full keyboard/mouse/controls input, inactive
   host isolation and vendor commands on both ports.
2. Hold a key/modifier/mouse button while switching with a separate key. Verify
   old-host all-up and new-host silence until each held source is released and
   pressed again; cover both halves and modules.
3. Verify active-host LEDs, boot/6KRO/NKRO keyboard protocols, scroll resolution,
   Windows scroll behavior and macro continuations.
4. Disconnect/reconnect either USB cable or bridge during transfers and switches;
   reboot either half; verify no replay, duplicate relative movement or false
   completion. A blocked drain must remain visibly blocked.
   With the bridge detached, verify `transport=bluetooth`, typing from both
   halves and key release through left USB. Restore the bridge and verify
   `transport=bridge` with a fresh activation; repeat while holding a key and
   while an ACK is pending. Disconnect the wireless peer and confirm left-host
   all-up cleanup, then reconnect without replaying held input. Measure wireless
   latency and check mouse/controls/LED feedback alongside keyboard reports.
5. Test sleep/resume and permitted/denied remote wakeup, including a suspended
   old host that resumes after switching away.
6. Stress UART loss/retries, endpoint cancellation, USB reconfiguration and
   configuration reload. Measure latency and queue/stack headroom with logs both
   enabled and disabled. Do not enable the feature by default before these pass.
