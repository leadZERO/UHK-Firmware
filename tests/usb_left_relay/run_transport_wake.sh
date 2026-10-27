#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source_root="${TRANSPORT_WAKE_SOURCE_DIR:-$repo_root}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$source_root" "$test_dir" <<'PY'
from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:])
source = (root / 'right/src/hid/transport.cpp').read_text()
start = source.rfind('static ', 0, source.index('wakeUsbHostIfNeeded()'))
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
pieces = [source[start:end]]
for kind in ('Keyboard', 'Mouse', 'Controls'):
    start = source.index('case ReportSink_Usb:', source.index('errno_t Hid_Send' + kind + 'Report('))
    end = source.index('break;', start) + len('break;')
    pieces.append('static int send' + kind + '(const fake_report *report) {\n'
                  '[[maybe_unused]] unsigned generation = 7;\n'
                  '[[maybe_unused]] auto payload = report;\n'
                  'int err = -ECONNRESET;\nswitch (ReportSink_Usb) {\n' +
                  source[start:end] + '\n}\nreturn err;\n}')
(out / 'transport_wake_under_test.inc').write_text('\n'.join(pieces) + '\n')
PY
for relay in 0 1; do
  flags=()
  if [ "$relay" = 1 ]; then flags+=(-DCONFIG_UHK_USB_LEFT_RELAY=1); fi
  "${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
    "${flags[@]}" -I "$test_dir" "$repo_root/tests/usb_left_relay/test_transport_wake.cpp" \
    -o "$test_dir/test-transport-wake-$relay"
  "$test_dir/test-transport-wake-$relay"
done
