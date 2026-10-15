#!/usr/bin/env python3
"""Prove both wake regressions fail with a pre-fix relay from a supplied Git ref."""
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("usage: python3 tests/usb_left_relay/run_wake_baseline.py <pre-fix-git-ref>")
root = Path(__file__).resolve().parents[2]
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
with tempfile.TemporaryDirectory(prefix="uhk-wake-baseline-") as directory:
    output = Path(directory)
    for name in ("usb_left_relay.c", "usb_left_relay.h", "usb_left_relay_protocol.c",
                 "usb_left_relay_protocol.h"):
        source = subprocess.check_output(
            ["git", "-C", str(root), "show", f"{sys.argv[1]}:device/src/{name}"])
        (output / name).write_bytes(source)
    for mode, expected in (("WAKE_IDLE_ONLY", "idle suspend: wake_calls=1"),
                           ("WAKE_REPLAY_ONLY", "rapid sleep replay: extra_wake_calls=1")):
        executable = output / mode
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            "-fsanitize=address,undefined", "-D" + mode, "-I" + str(output),
            "-I" + str(root / "device/src"), "-I" + str(root / "right/src"),
            str(output / "usb_left_relay_protocol.c"), str(output / "usb_left_relay.c"),
            str(root / "tests/usb_left_relay/test_wake.c"), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        print(result.stdout, end="", flush=True)
        print(result.stderr, end="", flush=True)
        if result.returncode != -6 or expected not in result.stdout:
            raise SystemExit(f"{mode}: expected the pre-fix wake assertion to abort, got {result.returncode}")
        print(f"{mode}: expected pre-fix assertion failure confirmed", flush=True)
