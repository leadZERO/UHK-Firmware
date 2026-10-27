# UHK80 dual USB: development guide

Experimental firmware: independent right/left USB hosts with the wired bridge
preferred and paired inter-half BLE fallback. Both halves need compatible relay
firmware. See the [implementation contract](uhk80-dual-usb-hosts-design.md) and
[scope and acceptance](uhk80-dual-usb-hosts-prd.md).

**Current candidate, upstream integration on 2026-10-27:** merge `b76c6ce72` combines official master `0c2a8d985` with the validated feature parent `8f8a5bac7`. Both halves were built, signature-checked and flashed, and report firmware `18.0.65535` / tag `b76c6ce72`. Upstream's reported user-config version is now `15.1.0`; both saved user/hardware configurations remain byte-identical. The development slot is disabled. USB Left is selected over Bluetooth with both USB cables on Windows and the bridge disconnected. Initial diagnostics show no pending/blocked route, clear sender give-up state, all-up reports and no held Windows keys. Right relay faults are zero; left has one historical fault during the sequential updates with current reason zero. The user confirmed responsive typing from both halves, letter/Backspace release and Shift release in this Bluetooth configuration. Post-test Windows samples found no held keys, canonical reports were all-up, give-up state stayed clear and fault counts did not increase. Right retry/duplicate counts reached 97 during the test; their timing was not measured.

The merge retains both scheduler event sets and upstream's wake-triggering report suppression around the feature's queued right-USB sends, avoiding duplicate mouse submission. A new regression compiles those production switch arms and wake helper with relay enabled/disabled; pre-merge source fails its wake/drop assertion. All nine relay runners, 36 version cases, independent transport regressions and existing release/wake negative controls passed. Both halves also build with the feature disabled; all four signatures verified. All 729 normalized source/test/document files matched the tested snapshot before building the exact merge commit. Images, ELFs, configurations, source manifests, test/build/upload logs, backups and the prior deployed pair are retained in `build/local-uhk80/upstream-merge-20261027/`. Bridge, host-switching, sleep/wake, Agent configuration saving and extended-use checks remain outstanding.

**Previous candidate, 2026-10-27 before upstream integration:** both halves were built and flashed from `07666ac93` plus the relay completion correction. A stale `UsbReportSender_GivenUp` flag was confirmed on the faulty hardware. The production sender could clear its legacy in-flight latch after accepting a fresh relay press, causing the delivery callback to ignore a valid DONE and lose the subsequent release. The callback now relies on the relay's validated active token and pending sequence, advances the delivered baseline and clears give-up state. No protocol/schema or saved configuration change was needed.

The new sanitizer regression drives the real sender into give-up, then verifies Enter/GUI, mouse-button and media-control releases, pending-report protection and stale/duplicate acknowledgment handling. It is included in `tests/usb_left_relay/run.sh`; the negative-control runner reproduces the stuck key on `07666ac93`. All eight relay runners and 36 version cases passed; both halves built with the feature enabled and disabled, and all four signatures verified. All 662 normalized production files match the reviewed build snapshot. Both runtime versions report `18.0.65535` / source tag `07666ac93`, with the RAM development slot off. Both saved configurations are unchanged. Exact images, ELFs, regression/negative-control logs, backups and the deployment record are retained in the ignored `build/local-uhk80/release-fix-20261027/` directory.

After deployment, the user confirmed normal Enter/letter/Backspace release and typing from both halves, with USB Left selected, both USB cables connected to Windows and the bridge disconnected. Four subsequent Windows samples found no held keys; both right canonical reports and the left completed keyboard report were all-up, `UsbReportSender_GivenUp=0`, and no report or route was pending. Right relay faults remained zero; the left historical fault count remained one with current reason zero. The 214 retry/duplicate counts observed after typing were stable in the final idle snapshot. This is a passing physical release check over Bluetooth; explicit modifier/mouse-button checks, bridge operation, host switching, sleep/wake and longer use remain unvalidated on this candidate. The earlier hardware failure's send-failure transition was not deliberately recreated on hardware.

**Previous candidate, 2026-10-15:** both halves were built and flashed from
`82b2a7167` plus the local remote-wake correction. Retained USB Left selection
no longer wakes a suspended host; fresh physical key/button actions or explicit
selection can request wake. Schema `0x01DD041E` requires this paired update.
Both halves report `18.0.65535`, with relay enabled and the development slot off.
All seven relay scripts and 36 compiled version cases passed. Both relay-enabled
and ordinary configurations for each half built and passed signature validation.
All 662 normalized production files matched the reviewed build snapshot. The
unchanged-source negative controls reproduced idle wake and delayed wake into a
later sleep; the corrected tests produced zero unintended wakes in both cases.

After deployment, USB Left negotiated over the bridge with no pending transfers,
blocked route or relay faults, and both saved configuration files were unchanged.
The user then disconnected only right USB, kept left USB and the bridge attached,
and tested Windows sleep: it stayed asleep until a deliberate UHK key press, then
woke with normal typing and release. Windows recorded sleep from 08:12:27 to
08:14:29 MDT on October 15 (about 122 seconds); `powercfg /lastwake` identified
the UHK left USB device (`VID_37A8&PID_0007`). This is one successful wired cycle;
repeated/overnight sleep and Bluetooth fallback wake remain untested on hardware.
The local ignored `build/local-uhk80/wake-fix-20261015/` directory retains images,
matching ELFs/configurations, source manifests, regression/negative-control logs,
upload logs, Windows wake evidence, configuration backups and rollback images.

**Earlier candidate, 2026-10-12:** both halves were built and flashed from
`19b8a2483` plus the provisional-version changes described below. Both report
`18.0.65535`, with relay enabled and the RAM-only development slot disabled.
All 719 normalized files in the deployed source manifest matched the checkout
before these validation notes were updated. All seven relay scripts, 36 compiled
version cases, and the independent NUS, UART and USB regressions passed; both
signed images passed verification. Saved configuration was unchanged by flashing.
The companion Agent then saved a persistent left-host entry successfully.

**Open hardware failure:** the user reported a key repeating after release on
USB Left with both USB cables connected to Windows and the bridge attached.
The first diagnostic capture followed a switch back to right USB, so it did not
capture the active failure. A subsequent retest with Agent closed and the original
wiring passed. USB Left was active over the bridge with no pending reports or
blocked route; the historical relay fault counter stayed at five. The cause is
unresolved, and the passing retest does not establish a fix or general readiness.
No firmware or saved configuration changes were made during that investigation.
Full physical acceptance and extended use remain pending.

**October 18 follow-up on the October 15 candidate:** the user reported repeating after release for physically left-half keys on macOS with USB Left selected and the bridge attached. The first capture again found right USB selected; the user confirmed cable/reset changes before the successful reproduction attempt. A subsequent 30-second left-half typing test with unchanged wiring passed. Working-state SRAM showed released left-key states and empty canonical keyboard reports; relay fault and UART retry/give-up counters did not increase during the stable retest interval. A host-only test of the existing scanner functions demonstrated that a dropped release bitmap can remain stale during idle scanning, including when an explicit resend is pending. That scanner file is unchanged from the local fork baseline; mocked transport loss does not establish the cause of this hardware incident. No production change or flash was made. The local ignored `build/local-uhk80/stuck-left-macos-20261018/INVESTIGATION.md` records the evidence, mock boundaries and procedure for capturing the next untouched failure. The intermittent hardware failure remains unresolved.

**October 27 active-failure capture on the October 15 candidate:** Windows was connected through left USB with the bridge disconnected and the halves communicating over Bluetooth. The left physical matrix was released, but its last completed report and Windows both held the left GUI modifier. After right USB was connected for diagnostics, the held report was Enter. Two right-side SRAM captures found all key-state flags and both canonical keyboard reports empty, while both halves' relay state retained the acknowledged Enter report and Windows still considered Enter held. No relay report was pending or route blocked. This establishes a report-state mismatch in the left-host path; the exact transition remains unproven. Investigate delivered-baseline handling and resets, including the relay callback's canonical-buffer swap versus the immutable snapshot used for right USB. The earlier scanner-loss hypothesis is not established by this capture. No corrective firmware was deployed. Evidence and capture boundaries are retained locally in `build/local-uhk80/stuck-left-windows-20261027/INVESTIGATION.md`.

The ignored local `build/local-uhk80/agent-compatible-20261012/` directory retains
the signed images, matching ELFs/configurations, source hashes/diff, build and
upload logs, and backups. Its `stuck-key-20261012/` directory retains the incident
record and captures. These local artifacts are not included in the PR.

**Earlier validation, 2026-10-10:** all seven scripts pass, including macro handover
checks with the feature enabled/disabled. Both experimental images build, pass
signature verification and are flashed; both normal UHK80 configurations also
build with the feature disabled. The flashed defaults are now `USB Left` and
`USB Right`, verified in the runtime connection list. Saved configuration is
unchanged, including the configured right name `PC`. Quoted left-name macros
completed left/PC/left transitions over the bridge with no pending/blocked route;
the fault counter stayed at 4 between first and final left selections. Left USB
is selected and ready. The user reported the preceding minimized candidate
working; the full hardware acceptance matrix remains pending.
Temporary reversions of completion identity, keyboard snapshots and NUS payload
ownership failed regression checks, so those dependencies remain.
The local ignored `build/local-uhk80/default-host-names/` directory preserves the
earlier images, source hashes/diff against `1abf7c1b1`, logs and configuration
backups. `build/local-uhk80/minimal-host-review/` retains the preceding working
images and removal-test evidence for rollback.

## Independent fixes in the fork baseline

Fork `master` includes the separately reviewed NUS send ownership fix
([#7](https://github.com/leadZERO/UHK-Firmware/pull/7)), USB completion metadata fix
([#8](https://github.com/leadZERO/UHK-Firmware/pull/8)), and UART timeout ownership fix
([#9](https://github.com/leadZERO/UHK-Firmware/pull/9)). This feature branch merges
that baseline. The USB patch and UART fix are inherited unchanged; the NUS diff
adds nonblocking relay sends while preserving ordinary send ownership and debug
semaphore watching. Use the baseline's single c2usb completion patch, not the
earlier feature-specific copy. The standalone PRs retain their hardware evidence
and validation limits; merging them does not complete the feature acceptance matrix.

**Integration validation, 2026-10-12:** all seven relay scripts and the independent
NUS, UART and USB regression runners pass after the merge, including the NUS
unchanged-source negative control and nonblocking relay sends with debug semaphore
watching enabled. The consolidated patch series applies to the pinned c2usb source.
The current-candidate paragraph above records the subsequent build, deployment
and limited physical retest; full feature acceptance remains incomplete.

## Build and test

Relay-enabled UHK80 halves now report the provisional firmware version
**18.0.65535**, controlled by `UHK_USB_LEFT_RELAY_FIRMWARE_VERSION` in
`shared/versioning.h`. Relay-disabled builds and other devices retain the normal
version from `scripts/package.json` (currently 18.0.4). Protocol/configuration
versions and the zero-version checksum-generation pass are unchanged. Earlier
experimental images reporting 18.0.4 do not satisfy the new Agent support gate;
rebuild and update both halves before testing that gate. Normal release packaging
still describes normal presets, not the experimental relay images.

Use a fresh Linux-filesystem workspace from the
[setup guide](uhk80-windows-wsl-setup.md): NCS 3.3.0, Zephyr SDK 0.17.4 and its
pinned Python requirements. Existing SDK workspaces used for reconnect diagnostics
may contain unrelated patches. Apply this branch's patches once to fresh pinned
SDK sources: c2usb `0001` (baseline diagnostics), c2usb `0002` (successful IN
completion identity/length), and Zephyr `0002` (assertion-warning cleanup).
Ordinary builds need neither `west update` nor patch reapplication.

Set the path below to that clean workspace and synchronize intended source edits
into its `firmware` checkout before building. Windows CRLF breaks shell tests;
keep the Linux checkout's source and scripts in LF format.

```bash
UHK_WORKSPACE=/absolute/path/to/clean/uhk-workspace
source "$UHK_WORKSPACE/activate-uhk80.sh"
cd "$UHK_WORKSPACE/firmware"
west build --sysbuild --build-dir device/build/uhk-80-right-usb-left device -- --preset uhk-80-right-usb-left
west build --sysbuild --build-dir device/build/uhk-80-left-usb-left device -- --preset uhk-80-left-usb-left
python ../bootloader/mcuboot/scripts/imgtool.py verify -k ../bootloader/mcuboot/root-ec-p256.pem device/build/uhk-80-right-usb-left/device/zephyr/zephyr.signed.bin
python ../bootloader/mcuboot/scripts/imgtool.py verify -k ../bootloader/mcuboot/root-ec-p256.pem device/build/uhk-80-left-usb-left/device/zephyr/zephyr.signed.bin

bash tests/usb_left_relay/run.sh
bash tests/usb_left_relay/run_c2usb_completion.sh
bash tests/usb_left_relay/run_relay_poll.sh
bash tests/usb_left_relay/run_normal_keyboard_completion.sh
bash tests/usb_left_relay/run_wireless.sh
bash tests/usb_left_relay/run_nus_contention.sh
bash tests/usb_left_relay/run_wireless_latency.sh
python3 tests/usb_left_relay/test_versions.py
```

Incremental builds use `west build --build-dir <same-directory>`. Debug presets
append `-debug`; use separate build directories. The
[test guide](../../tests/usb_left_relay/README.md) records coverage and mock limits.
To reproduce the current Agent-compatible candidate, append
`-Ddevice_CONFIG_UHK_USB_LEFT_DEV_SLOT=n` after the preset argument in each build
command above. Create the persistent left entry through the companion Agent;
the right development preset otherwise enables its temporary RAM-only entry.
Export each signed image with its matching ELF and `.config`; record source and
image hashes before flashing. `merged.hex` includes MCUboot and is not an
application USB-updater input.

## Native Windows update and rollback

Use a compatible native Agent checkout with dependencies installed; close other
Agent connections. Back up configuration and retain known-working signed
left/right images before updating. From that Agent checkout, set absolute paths
to the intended exports. The application updater preserves the bootloader.

```powershell
$uhkLeftImage = 'C:\path\to\left\zephyr.signed.bin'
$uhkRightImage = 'C:\path\to\right\zephyr.signed.bin'
.\node_modules\.bin\tsx.cmd .\packages\usb\update-device-firmware.ts --vid=14248 --pid=7 $uhkLeftImage
if ($LASTEXITCODE -ne 0) { throw 'Left update failed' }
.\node_modules\.bin\tsx.cmd .\packages\usb\update-device-firmware.ts --vid=14248 --pid=9 $uhkRightImage
if ($LASTEXITCODE -ne 0) { throw 'Right update failed' }
```

Verify both halves' reported versions and compare saved configuration afterward.
For rollback, set explicit official v18.0.4 left/right image paths and run the
same updater. Emergency bootloader recovery has not been validated here.

## Host selection and status

The right development preset automatically adds the RAM-only `USB Left` slot
when no left slot exists and a regular slot is empty. Existing named slots retain
their original name; duplicates are rejected. The right default is `USB Right`,
while configured names such as `PC` take precedence. Names containing spaces
must be quoted inside the macro. Existing `next`/`nextActive` bindings are unchanged.
Use existing macros from the native Agent checkout:

```powershell
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 "switchHost 'USB Left'"
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'switchHost nextActive'
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft status'
```

Select right by its configured `switchHost` name/index; cycling follows existing
host order and readiness rules. Selection is asynchronous. Reread status for
current/requested route, pending/blocked state, transport/generations, activation,
faults and provisioning result. `zephyr uhk connections` shows host connections.
An unproven drain stays blocked; a timeout is not evidence of safe cancellation.
The pinned Agent's config/protocol 14/4.17 differs from firmware 15/4.20; low-level
update/macro commands do not establish GUI configuration-writing compatibility.
The [Agent integration handoff](uhk80-dual-usb-hosts-design.md#agent-integration-handoff)
specifies defaults, saved-slot behavior and remaining release decisions for the
separate Agent repository.

Hardware acceptance in the [scope document](uhk80-dual-usb-hosts-prd.md#acceptance)
requires two-computer isolation, held-input release, macro ordering, bridge/BLE
handover, protocol/feedback and sleep/wake. Host regressions and firmware builds
do not establish physical USB cancellation or radio timing.

## Known baseline limitation

The [USB-C warm reconnect issue](https://github.com/leadZERO/UHK-Firmware/issues/1)
also reproduces on official v18.0.4: with right USB connected throughout, detach
the bridge, unplug left USB, reattach the bridge, then reconnect left directly
C-to-C. Left may fail to enumerate; cycling the bridge or using the verified
USB-A path restores it. The root cause is unproven and excluded from this feature.
Diagnostics and history are preserved on local `codex/uhk80-usb-c-reconnect` at
`fb9df3bc5`; inspect its development handoff with:

```bash
git show fb9df3bc5:doc-dev/technical/uhk80-dual-usb-hosts-development.md
```
