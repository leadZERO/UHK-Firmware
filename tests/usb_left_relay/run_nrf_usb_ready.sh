#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
workspace="$(cd -- "$repo_root/.." && pwd)"
zephyr_root="${ZEPHYR_SOURCE_DIR:-$workspace/zephyr}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$zephyr_root/drivers/usb/udc/udc_nrf.c" "$test_dir/nrf_usb_ready_under_test.inc" <<'PY'
import pathlib, sys
source = pathlib.Path(sys.argv[1]).read_text()
def function(marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
pathlib.Path(sys.argv[2]).write_text('\n'.join(function(m) for m in [
    'static void nrf_usbd_peripheral_enable(', 'static void nrf_usbd_legacy_start(', 'static void udc_nrf_power_handler(',
    'static int udc_nrf_enable(']))
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$test_dir" "$repo_root/tests/usb_left_relay/test_nrf_usb_ready.c" -o "$test_dir/test-nrf-ready"
"$test_dir/test-nrf-ready"
