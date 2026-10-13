# UHK80 dual USB: development guide

Experimental firmware: independent right/left USB hosts with the wired bridge
preferred and paired inter-half BLE fallback. Both halves need compatible relay
firmware. See the [implementation contract](uhk80-dual-usb-hosts-design.md) and
[scope and acceptance](uhk80-dual-usb-hosts-prd.md).

**Current candidate, 2026-10-12:** both halves were built and flashed from
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
