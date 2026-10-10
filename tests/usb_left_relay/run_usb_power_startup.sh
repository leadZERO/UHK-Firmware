#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source_root="${USB_POWER_SOURCE_DIR:-$repo_root}"
workspace="$(cd -- "$repo_root/.." && pwd)"
c2usb_root="${C2USB_SOURCE_DIR:-$workspace/c2usb}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$source_root" "$c2usb_root" "$test_dir" <<'PY'
import pathlib, re, sys
root, sdk, out = map(pathlib.Path, sys.argv[1:])
source = (root / 'right/src/hid/transport_usb.cpp').read_text()
expression = re.search(r'usb::zephyr::udc_mac mac_\{.*?, 256,\s*(.*?)\};', source, re.S)[1]
(out / 'usb_initial_power_under_test.inc').write_text(expression)
def function(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
source = (sdk / 'c2usb/usb/df/mac.cpp').read_text()
(out / 'usb_mac_start_under_test.inc').write_text(function(source, 'void mac::start()'))
(out / 'usb_mac_power_under_test.inc').write_text(function(source, 'void mac::set_power_state('))
source = (sdk / 'c2usb/port/zephyr/udc_mac.cpp').read_text()
(out / 'usb_attach_under_test.inc').write_text(function(source, 'bool udc_mac::set_attached(bool attached)'))
marker = 'void udc_mac::finish_control_retirement()'
(out / 'usb_control_barrier_under_test.inc').write_text(
    function(source, 'void udc_mac::retire_control()') + '\n' + function(source, marker) if marker in source else '')
if 'bool udc_mac::synchronize_vbus(' in source:
    (out / 'usb_reconcile_under_test.inc').write_text(function(source, 'bool udc_mac::synchronize_vbus(')
        + '\n' + function(source, 'int udc_mac::process_event('))
    source = (root / 'right/src/hid/transport_usb.cpp').read_text()
    (out / 'usb_power_barrier_under_test.inc').write_text(function(source, 'extern "C" void Hid_LocalUsbService(void)'))
    features = '#define TEST_VBUS_RECONCILE 1\n'
    marker = 'static void synchronizePowerOnUsbThread()'
    if marker in source:
        presence = 'static bool observedUsbPresence()'
        prefix = function(source, presence) + '\n' if presence in source else ''
        (out / 'usb_power_task_under_test.inc').write_text(prefix + function(source, marker))
        if prefix:
            features += '#define TEST_STOPPED_CONTROLLER 1\n'
        features += '#define TEST_POWER_TASK 1\n'
    else:
        (out / 'usb_power_task_under_test.inc').write_text('')
    (out / 'usb_power_features.inc').write_text(features)
else:
    (out / 'usb_power_features.inc').write_text('')
PY
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$test_dir" "$repo_root/tests/usb_left_relay/test_usb_power_startup.cpp" -o "$test_dir/test-usb-power"
"$test_dir/test-usb-power"
