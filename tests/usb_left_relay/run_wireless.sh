#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$repo_root" "$test_dir" <<'PY'
import pathlib, sys
root, out = map(pathlib.Path, sys.argv[1:])
source = (root / 'device/src/usb_left_relay_uhk.c').read_text()
def function(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
start = source.index('#define HALF_UART')
end = source.index('static int submit(', start)
pieces = [source[start:end]]
pieces += [function(source, marker) for marker in [
    'uint32_t UsbLeft_IngressGeneration(', 'void UsbLeft_LinkChanged(', 'void UsbLeft_Receive(']]
(out / 'relay_transport_under_test.inc').write_text('\n'.join(pieces))
process = function(source, 'void UsbLeft_Process(void)')
start = process.index('uint8_t connection = UsbLeft_LinkConnection();')
end = process.index('#if DEVICE_IS_UHK80_LEFT', start)
(out / 'relay_link_observation_under_test.inc').write_text(process[start:end])
messenger = (root / 'device/src/messenger.c').read_text()
(out / 'relay_messenger_under_test.inc').write_text(function(messenger, 'int Messenger_SendMessage('))
PY
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
       -I "$test_dir" -I "$repo_root/device/src")
for role in Right Left; do
    right=0; [ "$role" != Right ] || right=1
    "${CC:-cc}" "${flags[@]}" -DTEST_ROLE="$role" -DDEVICE_IS_UHK80_RIGHT="$right" \
      -c "$repo_root/tests/usb_left_relay/test_wireless_endpoint.c" -o "$test_dir/$role.o"
done
"${CC:-cc}" "${flags[@]}" "$repo_root/device/src/usb_left_relay_protocol.c" \
  "$repo_root/device/src/usb_left_relay.c" "$repo_root/tests/usb_left_relay/test_wireless.c" \
  "$test_dir/Right.o" "$test_dir/Left.o" -o "$test_dir/test-wireless"
"$test_dir/test-wireless"
