#!/usr/bin/env python3
"""Require the uncorrected sender/relay integration to reproduce the held key."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
revision = sys.argv[1] if len(sys.argv) > 1 else "07666ac93"
sources = [
    "device/src/usb_left_relay_protocol.c",
    "device/src/usb_left_relay.c",
    "device/src/usb_left_relay_uhk.c",
    "right/src/usb_report_sender.c",
    "right/src/usb_report_updater.c",
    "right/src/usb_semaphore.c",
    "right/src/hid/keyboard_report.c",
    "right/src/hid/mouse_report.c",
    "right/src/hid/controls_report.c",
]
with tempfile.TemporaryDirectory(prefix="uhk-relay-completion-baseline-") as temp:
    source_root = Path(temp)
    for relative in sources:
        dest = source_root / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(subprocess.check_output(
            ["git", "-C", str(root), "show", f"{revision}:{relative}"]))
    env = dict(os.environ, RELAY_COMPLETION_SOURCE_DIR=str(source_root))
    result = subprocess.run(["bash", str(root / "tests/usb_left_relay/run_relay_completion.sh")],
                            env=env, capture_output=True, text=True)
    output = result.stdout + result.stderr
    print(output, end="")
    expected = "after give-up Enter: host_released=0 given_up=1 confirmations=0"
    if result.returncode == 0 or expected not in output or "Assertion `released' failed" not in output:
        raise SystemExit("Baseline did not reach the expected held-key assertion")
    print(f"Confirmed baseline {revision} loses the release after sender give-up.")
