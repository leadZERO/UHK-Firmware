#!/usr/bin/env python3
"""Compile generated version declarations for enabled, disabled and zero builds.

Run with Python 3 and a C compiler. No SDK or hardware is required.
"""
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
package = json.loads((repo / 'scripts/package.json').read_text())

with tempfile.TemporaryDirectory(prefix='uhk-relay-versions-') as temporary:
    directory = Path(temporary)
    (directory / 'scripts').mkdir()
    (directory / 'shared').mkdir()
    # Exercise the real generator without overwriting the checkout's versions.c.
    for name in ('generate_versions.py', 'common.py'):
        shutil.copyfile(repo / 'scripts' / name, directory / 'scripts' / name)
    sys.path.insert(0, str(directory / 'scripts'))
    generator = runpy.run_path(str(directory / 'scripts/generate_versions.py'))
    prefixes = generator['version_property_prefixes']
    for name in ('autoconf.h', 'mcux_config.h'):
        (directory / name).write_text('/* Configuration is supplied by the test. */\n')
    prints = '\n'.join(
        f'printf("%u.%u.%u\\n", (unsigned){p}Version.major, '
        f'(unsigned){p}Version.minor, (unsigned){p}Version.patch);'
        for p in prefixes
    )
    (directory / 'probe.c').write_text(f'''#include <stdio.h>
#include "versioning.h"
int main(void) {{
    const version_t relay = UHK_USB_LEFT_RELAY_FIRMWARE_VERSION;
    printf("%u.%u.%u\\n", (unsigned)relay.major, (unsigned)relay.minor, (unsigned)relay.patch);
    {prints}
    printf("%s\\n%s\\n", gitRepo, gitTag);
    return 0;
}}
''')
    cases = 0
    for zero in (False, True):
        generator['generate_versions'](package, {'repo': 'test/repo', 'tag': 'test-tag'}, zero)
        # One generated file must work for every target and option combination.
        for device in (None, 1, 2, 3, 4, 5):
            for relay in (None, 0, 1):
                flags = []
                if device is not None:
                    flags.append(f'-DCONFIG_DEVICE_ID={device}')
                if device in (3, 4, 5):
                    flags.append('-D__ZEPHYR__')
                if relay is not None:
                    flags.append(f'-DCONFIG_UHK_USB_LEFT_RELAY={relay}')
                binary = directory / 'probe'
                subprocess.run([
                    os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wundef',
                    *flags, '-I', str(directory), '-I', str(repo / 'shared'),
                    str(directory / 'shared/versions.c'), str(directory / 'probe.c'),
                    '-o', str(binary)
                ], check=True)
                lines = subprocess.check_output([str(binary)], text=True).splitlines()
                marker, *actual = lines
                assert marker != package['firmwareVersion'], 'Relay version must differ from normal firmware'
                expected = ['0.0.0' if zero else package[f'{p}Version'] for p in prefixes]
                if not zero and device in (3, 4) and relay == 1:
                    expected[0] = marker
                expected.extend(['', ''] if zero else ['test/repo', 'test-tag'])
                assert actual == expected, (zero, device, relay, actual, expected)
                cases += 1
    print(f'PASS: {cases} compiled version cases; relay={marker}, ordinary={package["firmwareVersion"]}')
