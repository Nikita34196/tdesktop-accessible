#!/usr/bin/env python3
"""Remove duplicate Tab handler inside keyPressEvent (guard v2 handles Tab)."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.environ.get('REPO_NAME', 'tdesktop')
MARKER = 'a11y-tab-dedupe-keypress-v1'
CPP_PATH = f'{ROOT}/Telegram/SourceFiles/history/history_inner_widget.cpp'

INNER_TAB = re.compile(
    r'\n\t\tif \(e->key\(\) == Qt::Key_Tab\n'
    r'\t\t\t&& !\(e->modifiers\(\) & ~Qt::ShiftModifier\)\) \{\n'
    r'\t\t\ta11yMoveFocusedLink\(\n'
    r'\t\t\t\t\(e->modifiers\(\) & Qt::ShiftModifier\) \? -1 : 1\);\n'
    r'\t\t\te->accept\(\);\n'
    r'\t\t\treturn;\n'
    r'\t\t\}\n',
    re.MULTILINE,
)


def main() -> int:
    if not os.path.exists(CPP_PATH):
        print(f'WARNING: missing {CPP_PATH}')
        return 0

    with open(CPP_PATH, encoding='utf-8') as f:
        cpp = f.read()

    if MARKER in cpp:
        print('history_inner_widget.cpp: Tab dedupe already applied')
        return 0

    if 'a11y-tab-focus-guard-v2 keyPressEvent' not in cpp:
        print('WARNING: Tab focus guard v2 missing — skip dedupe')
        return 0

    cpp2, n = INNER_TAB.subn('\n', cpp, count=1)
    if n == 0:
        print('WARNING: duplicate inner Tab handler not found')
        return 0

    cpp2 = cpp2.replace(
        'void HistoryInner::keyPressEvent(QKeyEvent *e) {\n',
        'void HistoryInner::keyPressEvent(QKeyEvent *e) {\n'
        '\t// a11y-tab-dedupe-keypress-v1: Tab handled at top by focus guard v2.\n',
        1,
    )

    with open(CPP_PATH, 'w', encoding='utf-8') as f:
        f.write(cpp2)
    print('history_inner_widget.cpp: removed duplicate Tab handler')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
