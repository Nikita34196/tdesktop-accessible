#!/usr/bin/env python3
"""Patch tdesktop prepare.py so Qt imageformat plugins build reliably on GHA."""
from __future__ import annotations

import os
import sys

ROOT = os.environ.get('REPO_NAME', 'tdesktop')
PREPARE = f'{ROOT}/Telegram/build/prepare/prepare.py'
MARKER = 'a11y-ci-qt-modules-inst'

OLD = (
    '        -platform win32-msvc\n'
    '\n'
    '    jom -j%NUMBER_OF_PROCESSORS%\n'
    '    jom -j%NUMBER_OF_PROCESSORS% install\n'
    '""")\n'
    "else: # qt > '6'"
)

NEW = (
    '        -platform win32-msvc\n'
    '\n'
    f'    rem {MARKER}: pre-create plugin module dirs (jom race on GHA)\n'
    '    if not exist qtimageformats\\mkspecs\\modules-inst mkdir '
    'qtimageformats\\mkspecs\\modules-inst\n'
    '    if not exist qtsvg\\mkspecs\\modules-inst mkdir '
    'qtsvg\\mkspecs\\modules-inst\n'
    '    jom -j%NUMBER_OF_PROCESSORS%\n'
    '    jom -j%NUMBER_OF_PROCESSORS% install\n'
    '""")\n'
    "else: # qt > '6'"
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
        print(f'ERROR: Qt jom landmark not found in {PREPARE}')
        sys.exit(1)

    with open(PREPARE, 'w', encoding='utf-8') as f:
        f.write(src.replace(OLD, NEW, 1))
    print(f'{PREPARE} patched ({MARKER})')


if __name__ == '__main__':
    main()
