#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$repo_root/device/src/bt_conn.c" "$test_dir/relay_latency_under_test.inc" <<'PY'
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
    'static void configureLatency(', 'static latency_mode_t desiredLatencyMode(',
    'void BtConn_UpdateConnectionLatencies(void)']))
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -I "$test_dir" "$repo_root/tests/usb_left_relay/test_wireless_latency.c" -o "$test_dir/test-wireless-latency"
"$test_dir/test-wireless-latency"
