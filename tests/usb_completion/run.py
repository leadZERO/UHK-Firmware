#!/usr/bin/env python3
"""Test the actual pinned c2usb method before and after firmware patches.

Run: python3 tests/usb_completion/run.py /path/to/west/workspace
Requires Git and a C++20 host compiler. No device is accessed.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
workspace = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else repo.parent
sdk = workspace / "c2usb"
revision = re.search(r"revision:\s*(\w+)", (repo / "submanifests/c2usb.yml").read_text())[1]
relative = "c2usb/port/zephyr/udc_mac.cpp"
original = subprocess.check_output(["git", "-C", str(sdk), "show", f"{revision}:{relative}"]).decode()


def method(source):
    start = source.index("void udc_mac::process_ep_event(net_buf* buf)")
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


with tempfile.TemporaryDirectory(prefix="uhk-usb-completion-") as temporary:
    directory = Path(temporary)
    target = directory / relative
    target.parent.mkdir(parents=True)
    target.write_text(original)
    # Apply the same ordered patches as west patch, restricted to this source.
    for patch in sorted((repo / "patches/c2usb").glob("*.patch")):
        subprocess.run(["git", "apply", f"--include={relative}"],
                       cwd=directory, input=patch.read_text(), text=True, check=True)
    patched = target.read_text()
    for name, source in [("baseline", original), ("patched", patched)]:
        (directory / "c2usb_completion_under_test.inc").write_text(method(source))
        binary = directory / name
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
                        "-I", str(directory), "-I", str(sdk / "c2usb"),
                        "-I", str(workspace / "modules/lib/bitfilled/bitfilled"),
                        str(repo / "tests/usb_completion/test_completion.cpp"), "-o", str(binary)],
                       check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True)
        print(f"{name}: exit={result.returncode}\n{result.stdout}{result.stderr}", flush=True)
        if name == "baseline":
            assert result.returncode != 0 and "completed.data() == bytes.data()" in result.stderr
        else:
            assert result.returncode == 0
print("Pinned baseline fails the consumed-IN assertion; patched method passes with ASan/UBSan.")

