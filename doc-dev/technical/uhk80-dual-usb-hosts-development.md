# UHK80 dual USB: development guide

Experimental feature on `feature/uhk80-dual-usb-hosts`: independently selectable
right/left USB hosts, with UART bridge preferred and paired inter-half BLE
fallback. Both halves need the experimental firmware. See the
[implementation contract](uhk80-dual-usb-hosts-design.md) and
[release requirements](uhk80-dual-usb-hosts-prd.md).

**Validation state, 2026-10-10:** earlier experimental firmware demonstrated
Windows typing and key release through left USB, including BLE fallback over the
working USB-A connection path. All seven retained feature test scripts pass with
the separated SDK patch set; both cleaned firmware images build and pass MCUboot
signature verification. They have not been flashed. The physical keyboard still
runs the r11 diagnostic pair; prior hardware results do not validate this build.
Images, matching ELF/config, source hashes and logs are in the local ignored
`build/local-uhk80/feature-only-20261010/` directory. Right application usage is
856,336 bytes flash / 245,472 bytes RAM; left is 655,468 / 204,760 bytes.

## Build and test

Use the Linux-filesystem WSL checkout `/home/ryans/uhk-feature-only-20261010/firmware`;
Windows CRLF previously broke the shell tests. Use native Windows for USB tools.
The [setup guide](uhk80-windows-wsl-setup.md) records NCS 3.3.0, Zephyr SDK 0.17.4,
west 1.5.0 and fresh-workspace setup. Synchronize intended source edits explicitly;
`-UseLinuxCheckout` skips automatic synchronization and can build stale source.

The retained SDK patches are c2usb `0001` (existing diagnostics), c2usb `0002`
(successful IN completion buffer/length), and Zephyr `0002` (assertion-warning
cleanup). Apply this branch's patches once to fresh pinned SDK sources using the
setup guide. An SDK tree previously used for reconnect experiments also contains
unrelated patches; use a clean SDK baseline before feature validation. Ordinary
builds require neither `west update` nor patch reapplication.

```bash
source /home/ryans/uhk-workspace/activate-uhk80.sh
export ZEPHYR_BASE=/home/ryans/uhk-feature-only-20261010/zephyr
cd /home/ryans/uhk-feature-only-20261010/firmware
west build --sysbuild --build-dir device/build/uhk-80-right-usb-left-feature-only device -- --preset uhk-80-right-usb-left
west build --sysbuild --build-dir device/build/uhk-80-left-usb-left-feature-only device -- --preset uhk-80-left-usb-left
python ../bootloader/mcuboot/scripts/imgtool.py verify -k ../bootloader/mcuboot/root-ec-p256.pem device/build/uhk-80-right-usb-left-feature-only/device/zephyr/zephyr.signed.bin
python ../bootloader/mcuboot/scripts/imgtool.py verify -k ../bootloader/mcuboot/root-ec-p256.pem device/build/uhk-80-left-usb-left-feature-only/device/zephyr/zephyr.signed.bin
```

Incremental builds use `west build --build-dir <same-directory>`. Debug presets
append `-debug`; use separate build directories. Export each signed image with
its matching ELF and `.config`, and record hashes before flashing.
`merged.hex` contains MCUboot and is not an application USB-updater input.

Run all seven feature scripts from the Linux firmware root. The
[test guide](../../tests/usb_left_relay/README.md) describes coverage and mocks.

```bash
bash tests/usb_left_relay/run.sh
bash tests/usb_left_relay/run_c2usb_completion.sh
bash tests/usb_left_relay/run_relay_poll.sh
bash tests/usb_left_relay/run_normal_keyboard_completion.sh
bash tests/usb_left_relay/run_wireless.sh
bash tests/usb_left_relay/run_nus_contention.sh
bash tests/usb_left_relay/run_wireless_latency.sh
```

These check protocol/coordinator behavior, completion identity, idle scheduling,
normal keyboard release after delayed completion, transport selection, NUS
ownership/contention and wireless profile requests. They do not establish
physical USB cancellation timing, radio latency or two-computer input isolation.

## Native USB tools and runtime commands

Use a compatible native Agent checkout with dependencies installed. Close
competing Agent connections. VID is 14248; application PID is 7 for left and 9
for right. Back up configuration and retain known-working signed images before
updating. The application updater preserves the installed bootloader.

From that Agent checkout, with absolute paths to the intended signed exports:

```powershell
$uhkLeftImage = 'C:\path\to\left\zephyr.signed.bin'
$uhkRightImage = 'C:\path\to\right\zephyr.signed.bin'
.\node_modules\.bin\tsx.cmd .\packages\usb\update-device-firmware.ts --vid=14248 --pid=7 $uhkLeftImage
if ($LASTEXITCODE -ne 0) { throw 'Left update failed' }
.\node_modules\.bin\tsx.cmd .\packages\usb\update-device-firmware.ts --vid=14248 --pid=9 $uhkRightImage
if ($LASTEXITCODE -ne 0) { throw 'Right update failed' }
```

Verify both halves' reported versions and compare saved configuration afterward.
For rollback, use explicit official v18.0.4 left/right image paths with the same
updater. Local `Uhk80.ps1` rollback shortcuts still reference v17.2.0 and generic
flash shortcuts can reference old exports; verify their inputs before use.
Emergency bootloader recovery has not been validated in this experiment.

The right preset adds the RAM-only `USB_Left_Dev` slot if no unique left slot
already exists. From the native Agent checkout:

```powershell
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft status'
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft select left'
.\node_modules\.bin\tsx.cmd .\packages\usb\exec-macro-command.ts --vid=14248 --pid=9 'zephyr uhk usbLeft select right'
```

Also available: `zephyr uhk usbLeft provision`, `zephyr uhk usbLeft retry`,
`zephyr uhk connections`, `zephyr uhk uartStats` and `zephyr uhk log usbSink 1`.
Selection is asynchronous; reread status for selected/requested route, blocked
state, transport/link/USB generations, availability, activation and fault counters.
Existing `switchHost nextActive` selects ready left; `switchHost USB_Left_Dev`
uses the development slot's name. An existing named slot keeps its original name.
The pinned Agent's config/protocol 14/4.17 differs from firmware 15/4.20; working
low-level update/macro commands do not validate GUI configuration rewriting.

## Hardware acceptance still required

- Two separate computers: both-half and module input reaches only the selected
  host, including held keys/modifiers/buttons, rapid switching and macro barriers.
- Keyboard protocols, mouse/controls, LEDs/scroll feedback and suspend/wakeup.
- Bridge preference, BLE-only operation, bridge restoration, radio/link loss and
  fresh-session recovery with no stale typing, movement or stuck releases.
- USB cancellation/reconfiguration, host reconnect and Windows/macOS/Linux
  coverage, with the known baseline limitation tracked separately.
- PRD 1,000-switch and 60-minute soak, latency, stack/queue and resource measures.

## Known baseline limitation

The [USB-C warm reconnect issue](https://github.com/leadZERO/UHK-Firmware/issues/1) also reproduces on
official v18.0.4: with right USB connected throughout, detach the bridge, unplug
left USB, reattach the bridge, then reconnect left directly C-to-C. Left may fail
to enumerate; cycling the bridge or using the verified USB-A path restores it.
The root cause is unproven. Full r11 diagnostics, patches, tests and historical
design/development archives are preserved on the local investigation branch
`codex/uhk80-usb-c-reconnect` at `fb9df3bc5`; inspect its handoff with
`git show fb9df3bc5:doc-dev/technical/uhk80-dual-usb-hosts-development.md`.
The feature branch does not depend on those reconnect experiments.
