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
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$repo_root/device/src" -I "$repo_root/right/src" \
  "$repo_root/device/src/usb_left_relay_protocol.c" \
  "$repo_root/device/src/usb_left_relay.c" \
  "$repo_root/tests/usb_left_relay/test_relay.c" -o "$test_dir/test-relay"
"$test_dir/test-relay"
for relay_enabled in 0 1; do
  flags=()
  if (( relay_enabled )); then flags+=(-DCONFIG_UHK_USB_LEFT_RELAY=1); fi
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "${flags[@]}" -I "$test_dir" -I "$repo_root/device/src" -I "$repo_root/right/src" -I "$repo_root/shared" \
    "$repo_root/tests/usb_left_relay/test_host_route.c" -o "$test_dir/test-host-route"
  "$test_dir/test-host-route"
done
