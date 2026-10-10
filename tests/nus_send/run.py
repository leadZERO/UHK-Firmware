#!/usr/bin/env python3
"""Run host-side NUS send regressions against actual production functions."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", help="Read source from this Git revision")
    parser.add_argument("--expect-failure", action="store_true",
                        help="Require every regression to fail (negative control)")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="uhk-nus-send-") as tmp:
        directory = Path(tmp)
        for name, server, lifetime in [("client-lifetime", 0, 1),
                                       ("client-ownership", 0, 0),
                                       ("server-ownership", 1, 0)]:
            role = "server" if server else "client"
            path = f"device/src/nus_{role}.c"
            source = (subprocess.check_output(
                ["git", "-C", str(ROOT), "show", f"{args.source_ref}:{path}"],
                text=True) if args.source_ref else (ROOT / path).read_text())
            marker = ("int NusServer_SendMessageTo(" if server else "int NusClient_SendMessage(")
            (directory / "send_under_test.inc").write_text(
                function(source, "static int send_raw_buffer(") + function(source, marker))
            binary = directory / name
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-g", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(directory), f"-DTEST_SERVER={server}",
                            f"-DTEST_LIFETIME={lifetime}",
                            str(ROOT / "tests/nus_send/test_send.c"), "-o", str(binary)], check=True)
            run = subprocess.run([str(binary)], text=True, capture_output=True,
                                 env={**os.environ, "ASAN_OPTIONS": "detect_stack_use_after_return=1"})
            output = run.stdout + run.stderr
            print(f"{name}: exit {run.returncode}\n{output}", flush=True)
            if args.expect_failure:
                assert run.returncode != 0 and ("Assertion" in output or
                                                "stack-use-after-return" in output), name
            else:
                assert run.returncode == 0, name


if __name__ == "__main__":
    main()
