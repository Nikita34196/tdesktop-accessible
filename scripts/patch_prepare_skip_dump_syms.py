#!/usr/bin/env python3
"""Skip breakpad dump_syms in prepare.py on CI (ATL-heavy; not needed for Telegram.exe)."""
from __future__ import annotations

import os
import sys

ROOT = os.environ.get('REPO_NAME', 'tdesktop')
PREPARE = f'{ROOT}/Telegram/build/prepare/prepare.py'
MARKER = 'a11y-ci-skip-dump-syms'

OLD = (
    '    cd tools\\\\windows\\\\dump_syms\n'
    '    gyp dump_syms.gyp --format=msvs\n'
    '    msbuild -m dump_syms.vcxproj /property:Configuration=Release '
    '/property:Platform="x64" %ToolsetProp%'
)

NEW = (
    f'    rem {MARKER}: dump_syms needs ATL; Telegram link only needs breakpad client\n'
    f'    rem cd tools\\\\windows\\\\dump_syms\n'
    f'    rem gyp dump_syms.gyp --format=msvs\n'
    f'    rem msbuild -m dump_syms.vcxproj /property:Configuration=Release '
    f'/property:Platform="x64" %ToolsetProp%'
)


def main() -> None:
    if not os.path.exists(PREPARE):
        print(f'WARNING: missing {PREPARE}')
        return

    with open(PREPARE, encoding='utf-8') as f:
        src = f.read()

    if MARKER in src:
        print(f'{PREPARE} already patched ({MARKER})')
        return

    if OLD not in src:
        print(f'ERROR: breakpad dump_syms landmark not found in {PREPARE}')
        sys.exit(1)

    with open(PREPARE, 'w', encoding='utf-8') as f:
        f.write(src.replace(OLD, NEW, 1))
    print(f'{PREPARE} patched ({MARKER})')


if __name__ == '__main__':
    main()
