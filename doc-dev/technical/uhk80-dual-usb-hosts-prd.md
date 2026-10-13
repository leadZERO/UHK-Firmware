# UHK80 dual USB: firmware scope and acceptance

Experimental firmware on `feature/uhk80-dual-usb-hosts`, disabled by default.
Connect one computer to each half's USB port and select which receives input.
The right half remains the controller for keymaps, layers, macros and host
selection. Both halves must run compatible relay firmware.

The [implementation contract](uhk80-dual-usb-hosts-design.md) describes ownership
and safety invariants; the [development guide](uhk80-dual-usb-hosts-development.md)
contains build, test, update and rollback commands.

## Required behavior

- USB Right and USB Left are independent host destinations. Keys on either half,
  attached modules, macros, pointer movement, scroll and consumer/system controls
  reach only the selected host. Both USB ports retain vendor/update access.
- Prefer the wired inter-half bridge. When absent, use the existing paired,
  encrypted inter-half BLE link. A transport change requires fresh negotiation
  and safe handover; losing both links makes USB Left unavailable.
- Use existing `switchHost` name/index/cycling commands. Explicit selection may
  choose an offline left host; input is discarded until it becomes ready.
  Active-host cycling considers left only when ready.
- A switch involving left blocks output, drains outstanding transfers, releases
  old-host held state, prepares the new route and then commits. Subsequent macro
  output waits at the transition barrier. An unproven USB drain remains blocked;
  elapsed time cannot authorize reuse of a transfer or unsafe route activation.
- Suppress physical keys, modifiers and mouse buttons held during handover until
  release/fresh press, including the switch key. Preserve keymaps and layers.
  Discard unavailable-route input and relative motion instead of replaying it.
- Keep each USB host's keyboard protocol/rollover and feedback independent.
  Selected-host LEDs and scroll resolution apply to that route. Account for
  suspend/resume and wake permission, including deferred neutral reports.
- Reject stale sessions, duplicate delivery and old completion callbacks.
  Backpressure must preserve accepted transitions and relative motion while the
  route is healthy. Loss or overflow must recover without stuck held state or
  blocking the scanner, and expose fault/status information.

## Configuration and compatibility

`CONFIG_UHK_USB_LEFT_RELAY` is opt-in on UHK80 left/right. The development right
preset also enables `CONFIG_UHK_USB_LEFT_DEV_SLOT`, which reuses one existing
USB Left slot or adds `USB Left` in an empty regular slot in RAM. Reject
ambiguous duplicate slots and full capacity; preserve reserved/paired slots,
saved configuration bytes, host indices and bindings. Reapply the RAM slot after
successful configuration reload. USB descriptors, VID/PIDs and bootloader stay
unchanged. Default display names are `USB Left` and `USB Right`; configured names
take precedence. Agent GUI or saved-configuration migration work is outside this
change; the [Agent handoff](uhk80-dual-usb-hosts-design.md#agent-integration-handoff)
records the existing firmware contract for that separate repository.

## Acceptance

1. Build signed left/right images with the pinned SDK and verify signatures.
   Run all seven retained feature test scripts and relevant baseline builds.
2. On two separate computers, verify both-half/module keyboard, mouse/scroll,
   macros and controls reach only the selected destination. Exercise existing
   host-selection commands, unavailable hosts and rapid changes of selection.
3. Hold keys, modifiers and mouse buttons across switches. Confirm release on
   the old host, no held-input leakage to the new host, fresh-press recovery and
   correct macro ordering. Check pending/failed transfers cannot bypass drain.
4. Verify bridge preference, BLE-only operation, bridge restoration, loss of
   both links and half reboot. Recovery uses fresh sessions without stale input,
   repeated relative motion or stuck releases.
5. Exercise keyboard protocols, LEDs, scroll feedback, independent suspend/wake,
   USB reconfiguration and both-host power states. Record host OS and wiring.
6. Verify right-only USB, existing BLE/dongle selection, configuration reload and
   native USB updates. Confirm saved configuration after update and preserve a
   reproducible rollback using known-working signed images.

Host tests use mocked transports and do not establish physical USB cancellation,
radio timing or two-computer isolation. The user reports the minimized candidate
working; the full acceptance matrix remains pending. Record actual results
without inventing latency or soak budgets. The baseline [USB-C warm reconnect issue](https://github.com/leadZERO/UHK-Firmware/issues/1)
also reproduces on official firmware and is excluded from this feature's scope.
