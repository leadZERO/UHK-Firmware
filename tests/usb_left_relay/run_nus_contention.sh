#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
source_root="${NUS_SOURCE_DIR:-$repo_root}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
python3 - "$source_root" "$test_dir" <<'PY'
import pathlib, sys
root, out = map(pathlib.Path, sys.argv[1:])
def function(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
for role in ['client', 'server']:
    source = (root / f'device/src/nus_{role}.c').read_text()
    send = 'int NusClient_SendMessage(' if role == 'client' else 'int NusServer_SendMessageTo('
    pieces = [function(source, 'static int send_raw_buffer('), function(source, send)]
    (out / f'nus_{role}_under_test.inc').write_text('\n'.join(pieces))
PY
for server in 0 1; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
      -DTEST_SERVER="$server" -I "$test_dir" \
      "$repo_root/tests/usb_left_relay/test_nus_contention.c" -o "$test_dir/test-nus-$server"
    ASAN_OPTIONS=detect_stack_use_after_return=1 "$test_dir/test-nus-$server"
done
