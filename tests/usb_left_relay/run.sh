#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$repo_root" "$test_dir" <<'PY'
import pathlib, sys
root, output = map(pathlib.Path, sys.argv[1:])
source = (root / 'device/src/usb_left_relay_uhk.c').read_text()
start = source.index('uint32_t localGeneration = Hid_LocalUsbGeneration();')
end = source.index('HostRoute_Process();', start)
(output / 'usb_generation_under_test.inc').write_text(source[start:end])
source = (root / 'right/src/macros/commands.c').read_text()
start = source.index('static macro_result_t processSwitchHostCommand(')
end = source.index('static macro_result_t processZephyrCommand(', start)
(output / 'switch_host_under_test.inc').write_text(source[start:end])
source = (root / 'right/src/macros/typedefs.h').read_text()
start = source.rindex('typedef enum {', 0, source.index('MacroResult_InProgressFlag'))
end = source.index('} macro_result_t;', start) + len('} macro_result_t;')
(output / 'macro_result_under_test.inc').write_text(source[start:end])
def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'
source = (root / 'right/src/usb_report_updater.c').read_text()
start = source.index('static uint8_t suppressedSources[')
end = source.index('void UsbReportUpdater_ResetHostInput(', start)
(output / 'wake_sources_under_test.inc').write_text(source[start:end])
reset = function(source, 'void UsbReportUpdater_ResetHostInput(')
start = reset.index('    if (suppressHeld)')
end = reset.index('    memset(keyboardReports', start)
(output / 'capture_input_under_test.inc').write_text(reset[start:end])
signatures = ['static bool suppressedSource(', 'static bool wakeSourceIndex(',
    'static void observeWakePress(', 'static void requestWakeOnAction(',
    'void ApplyKeyAction(', 'static void commitKeyState(',
    'static inline void preprocessKeyState(', 'static void updateActionStates(',
    'void UsbReportUpdater_UpdateActiveReports(']
(output / 'wake_updater_under_test.inc').write_text(''.join(function(source, s) for s in signatures))
source = (root / 'right/src/usb_report_sender.c').read_text()
(output / 'wake_sender_under_test.inc').write_text(function(source, 'void UsbReportSender_UpdateAndSendUsbReports('))
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$repo_root/device/src" -I "$repo_root/right/src" \
  "$repo_root/device/src/usb_left_relay_protocol.c" \
  "$repo_root/device/src/usb_left_relay.c" \
  "$repo_root/tests/usb_left_relay/test_relay.c" -o "$test_dir/test-relay"
"$test_dir/test-relay"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$repo_root/device/src" -I "$repo_root/right/src" \
  "$repo_root/device/src/usb_left_relay_protocol.c" \
  "$repo_root/device/src/usb_left_relay.c" \
  "$repo_root/tests/usb_left_relay/test_wake.c" -o "$test_dir/test-wake"
"$test_dir/test-wake"
for relay_enabled in 0 1; do
  flags=()
  if (( relay_enabled )); then flags+=(-DCONFIG_UHK_USB_LEFT_RELAY=1); fi
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "${flags[@]}" -I "$test_dir" -I "$repo_root/device/src" -I "$repo_root/right/src" -I "$repo_root/shared" \
    "$repo_root/tests/usb_left_relay/test_host_route.c" -o "$test_dir/test-host-route"
  "$test_dir/test-host-route"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined \
  -DCONFIG_UHK_USB_LEFT_RELAY=1 -DCONFIG_KEYBOARD_MAX_SCANCODE=0xdd \
  -I "$test_dir" -I "$repo_root/device/src" -I "$repo_root/right/src" -I "$repo_root/shared" \
  "$repo_root/tests/usb_left_relay/test_wake_input.c" -o "$test_dir/test-wake-input"
"$test_dir/test-wake-input"
bash "$repo_root/tests/usb_left_relay/run_relay_completion.sh"
