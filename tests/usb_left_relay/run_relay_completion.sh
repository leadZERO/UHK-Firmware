#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source_root="${RELAY_COMPLETION_SOURCE_DIR:-$repo_root}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$source_root" "$test_dir" <<'PY'
import pathlib, sys
root, out = map(pathlib.Path, sys.argv[1:])
def function(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'
sender = (root / 'right/src/usb_report_sender.c').read_text()
constants = sender[sender.index('#define THROTTLE_MAX_RETRIES'):sender.index('static void handleUltimateFail(')]
(out / 'sender_under_test.inc').write_text(constants + ''.join(function(sender, signature) for signature in [
    'bool UsbReportSender_ResendOrGiveUp(', 'static bool mouseButtonsChanged(', 'static void sendActiveReports(']))
semaphore = (root / 'right/src/usb_semaphore.c').read_text()
(out / 'semaphore_under_test.inc').write_text(''.join(function(semaphore, signature) for signature in [
    'bool UsbSemaphore_AnyInFlight(', 'void UsbSemaphore_Clear(', 'void UsbSemaphore_Release(',
    'void UsbSemaphore_Confirm(', 'bool UsbSemaphore_RecalculateIsReady(']))
updater = (root / 'right/src/usb_report_updater.c').read_text()
(out / 'buffers_under_test.inc').write_text(''.join(function(updater, signature) for signature in [
    'void UsbReportUpdater_SwitchActiveKeyboardReport(', 'void UsbReportUpdater_SwitchActiveMouseReport(',
    'void UsbReportUpdater_SwitchActiveControlsReport(']))
adapter = (root / 'device/src/usb_left_relay_uhk.c').read_text()
(out / 'relay_delivery_under_test.inc').write_text(function(adapter, 'static void delivered('))
comparisons = [('keyboard_report.c', 'bool KeyboardReport_HasChange('),
               ('mouse_report.c', 'bool MouseReport_HasChanges('),
               ('controls_report.c', 'bool ControlsReport_HasChanges(')]
(out / 'comparisons_under_test.inc').write_text(''.join(function(
    (root / 'right/src/hid' / file).read_text(), signature) for file, signature in comparisons))
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-but-set-variable -g -fsanitize=address,undefined \
  -DCONFIG_UHK_USB_LEFT_RELAY=1 -DCONFIG_KEYBOARD_MAX_SCANCODE=0xdd -DDEVICE_IS_MASTER=1 \
  -I "$test_dir" -I "$repo_root/device/src" -I "$repo_root/right/src" -I "$repo_root/shared" \
  "$source_root/device/src/usb_left_relay_protocol.c" "$source_root/device/src/usb_left_relay.c" \
  "$repo_root/tests/usb_left_relay/test_relay_completion.c" -o "$test_dir/test-relay-completion"
"$test_dir/test-relay-completion"
