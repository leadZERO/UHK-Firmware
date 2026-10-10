#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
workspace="$(cd -- "$repo_root/.." && pwd)"
c2usb_root="${C2USB_SOURCE_DIR:-$workspace/c2usb}"
zephyr_root="${ZEPHYR_SOURCE_DIR:-$workspace/zephyr}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$c2usb_root" "$zephyr_root" "$test_dir" <<'PY'
import pathlib, re, sys
c2, zephyr, out = map(pathlib.Path, sys.argv[1:])
# The actual pinned NCS3.3 compatibility branch owns fixed control buffers.
# Fail loudly if the SDK changes to the driver-managed control API.
header = (zephyr / 'include/zephyr/drivers/usb/udc.h').read_text()
assert re.search(r'bool\s+setup_valid\s*;', header), 'test requires the pinned legacy control API'
def function(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
source = (c2 / 'c2usb/port/zephyr/udc_mac.cpp').read_text()
parts = [function(source, 'bool udc_mac::set_attached(bool attached)'),
         function(source, 'int udc_mac::process_event('),
         function(source, 'void udc_mac::ctrl_setup()')]
marker = 'void udc_mac::finish_control_retirement()'
if marker in source:
    parts.append(function(source, 'void udc_mac::retire_control()'))
    parts.append(function(source, marker))
    features = '#define TEST_CONTROL_RETIREMENT 1\n'
    if 'void udc_mac::try_queue_control_retirement()' in source:
        features += '#define TEST_NONBLOCK_CONTROL 1\n'
        for marker in ['void udc_mac::try_queue_control_retirement()',
                       'void udc_mac::retry_control_retirements()',
                       'void udc_mac::worker(', 'int udc_mac::event_callback(']:
            parts.append(function(source, marker))
    (out / 'usb_control_features.inc').write_text(features)
else:
    (out / 'usb_control_features.inc').write_text('')
(out / 'usb_control_mac.inc').write_text('\n'.join(parts))
source = (zephyr / 'drivers/usb/udc/udc_common.c').read_text()
parts = [function(source, 'int udc_disable(const struct device *dev)'),
         function(source, 'void udc_setup_received('),
         function(source, 'int udc_ep_enqueue('),
         function(source, 'int udc_ep_dequeue(')]
(out / 'usb_control_common.inc').write_text('\n'.join(parts))
PY
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$test_dir" "$repo_root/tests/usb_left_relay/test_usb_control_retirement.cpp" -o "$test_dir/test-usb-control"
for scenario in ${USB_CONTROL_SCENARIOS:-retained cached late closed reset reset_cached full}; do
    "$test_dir/test-usb-control" "$scenario"
done
