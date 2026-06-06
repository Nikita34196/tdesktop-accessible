#!/usr/bin/env python3
"""Intercept Tab on voice/audio before any textState scan (eventHook + keyPressEvent)."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.environ.get('TDESKTOP_ROOT', 'tdesktop')
GUARD_MARKER = 'a11y-tab-playback-guard'
TAB_FOCUS_GUARD_MARKER = 'a11y-tab-focus-guard-v2'
CPP_PATH = f'{ROOT}/Telegram/SourceFiles/history/history_inner_widget.cpp'

KEYPRESS_GUARD_V1 = (
    '\tif (e->key() == Qt::Key_Tab\n'
    '\t\t&& !(e->modifiers() & ~Qt::ShiftModifier)\n'
    '\t\t&& _accessibilityFocusedItem\n'
    '\t\t&& a11yShouldSkipTextStateTabScan(_accessibilityFocusedItem)) {\n'
    '\t\t// a11y-tab-playback-guard keyPressEvent\n'
    '\t\ta11yTabOnPlaybackMessage();\n'
    '\t\te->accept();\n'
    '\t\treturn;\n'
    '\t}\n'
)

KEYPRESS_GUARD_V2 = (
    '\tif (e->key() == Qt::Key_Tab\n'
    '\t\t&& !(e->modifiers() & ~Qt::ShiftModifier)\n'
    '\t\t&& _accessibilityFocusedItem) {\n'
    '\t\t// a11y-tab-focus-guard-v2 keyPressEvent\n'
    '\t\tif (a11yShouldSkipTextStateTabScan(_accessibilityFocusedItem)) {\n'
    '\t\t\ta11yTabOnPlaybackMessage();\n'
    '\t\t} else {\n'
    '\t\t\ta11yMoveFocusedLink(\n'
    '\t\t\t\t(e->modifiers() & Qt::ShiftModifier) ? -1 : 1);\n'
    '\t\t}\n'
    '\t\te->accept();\n'
    '\t\treturn;\n'
    '\t}\n'
)

EVENTHOOK_TAB_NEW = (
    '\t// a11y-link-tab-eventhook: catch Tab before Qt focus traversal.\n'
    '\tif (e->type() == QEvent::KeyPress) {\n'
    '\t\tauto *key = static_cast<QKeyEvent*>(e);\n'
    '\t\tif (key->key() == Qt::Key_Tab\n'
    '\t\t\t\t&& !(key->modifiers() & ~Qt::ShiftModifier)\n'
    '\t\t\t\t&& accessibilityChildCount() > 0) {\n'
    '\t\t\t// a11y-tab-playback-guard eventHook\n'
    '\t\t\tif (_accessibilityFocusedItem\n'
    '\t\t\t\t\t&& a11yShouldSkipTextStateTabScan(\n'
    '\t\t\t\t\t\t_accessibilityFocusedItem)) {\n'
    '\t\t\t\ta11yTabOnPlaybackMessage();\n'
    '\t\t\t} else {\n'
    '\t\t\t\ta11yMoveFocusedLink(\n'
    '\t\t\t\t\t(key->modifiers() & Qt::ShiftModifier) ? -1 : 1);\n'
    '\t\t\t}\n'
    '\t\t\tkey->accept();\n'
    '\t\t\treturn true;\n'
    '\t\t}\n'
    '\t}\n'
)


def patch_key_press_event(cpp: str) -> tuple[str, bool]:
    if TAB_FOCUS_GUARD_MARKER in cpp:
        return cpp, False
    if 'a11yTabOnPlaybackMessage' not in cpp:
        print('WARNING: a11yTabOnPlaybackMessage missing — run phase2 patch first')
        return cpp, False
    if KEYPRESS_GUARD_V1 in cpp:
        cpp2 = cpp.replace(KEYPRESS_GUARD_V1, KEYPRESS_GUARD_V2, 1)
        if cpp2 != cpp:
            return cpp2, True
    if 'a11y-tab-playback-guard keyPressEvent' in cpp:
        cpp2 = re.sub(
            r'\tif \(e->key\(\) == Qt::Key_Tab\n'
            r'\t\t&& !\(e->modifiers\(\) & ~Qt::ShiftModifier\)\n'
            r'\t\t&& _accessibilityFocusedItem\n'
            r'\t\t&& a11yShouldSkipTextStateTabScan\(_accessibilityFocusedItem\)\) \{\n'
            r'\t\t// a11y-tab-playback-guard keyPressEvent\n'
            r'\t\ta11yTabOnPlaybackMessage\(\);\n'
            r'\t\te->accept\(\);\n'
            r'\t\treturn;\n'
            r'\t\}\n',
            KEYPRESS_GUARD_V2,
            cpp,
            count=1,
        )
        if cpp2 != cpp:
            return cpp2, True
    cpp2, n = re.subn(
        r'(void\s+HistoryInner::keyPressEvent\s*\(\s*QKeyEvent\s*\*\s*e\s*\)\s*\{\n)',
        lambda m: m.group(1) + KEYPRESS_GUARD_V2,
        cpp,
        count=1,
    )
    if n == 0:
        print('ERROR: HistoryInner::keyPressEvent landmark not found')
        sys.exit(1)
    return cpp2, True


def patch_event_hook(cpp: str) -> tuple[str, bool]:
    if 'a11y-tab-playback-guard eventHook' in cpp:
        return cpp, False

    # Upgrade existing a11y-link-tab-eventhook block from workflow step 7.
    old_block = re.compile(
        r'\t// a11y-link-tab-eventhook: catch Tab before Qt focus traversal\.\n'
        r'\tif \(e->type\(\) == QEvent::KeyPress\) \{\n'
        r'\t\tauto \*key = static_cast<QKeyEvent\*>\(e\);\n'
        r'\t\tif \(key->key\(\) == Qt::Key_Tab\n'
        r'\t\t\t\t&& !\(key->modifiers\(\) & ~Qt::ShiftModifier\)\n'
        r'\t\t\t\t&& accessibilityChildCount\(\) > 0\) \{\n'
        r'\t\t\ta11yMoveFocusedLink\(\n'
        r'\t\t\t\t\(key->modifiers\(\) & Qt::ShiftModifier\) \? -1 : 1\);\n'
        r'\t\t\tkey->accept\(\);\n'
        r'\t\t\treturn true;\n'
        r'\t\t\}\n'
        r'\t\}\n',
        re.MULTILINE,
    )
    if old_block.search(cpp):
        cpp2, n = old_block.subn(EVENTHOOK_TAB_NEW, cpp, count=1)
        if n:
            return cpp2, True

    # Fresh eventHook without our Tab hook yet.
    if 'a11y-link-tab-eventhook' not in cpp:
        cpp2, n = re.subn(
            r'(bool\s+HistoryInner::eventHook\s*\(\s*QEvent\s*\*\s*e\s*\)\s*\{\n)',
            lambda m: m.group(1) + EVENTHOOK_TAB_NEW,
            cpp,
            count=1,
        )
        if n == 0:
            print('ERROR: HistoryInner::eventHook landmark not found')
            sys.exit(1)
        return cpp2, True

    print('ERROR: unrecognized eventHook Tab block')
    sys.exit(1)


def main() -> None:
    if not os.path.exists(CPP_PATH):
        print(f'WARNING: missing {CPP_PATH}')
        return

    with open(CPP_PATH, encoding='utf-8') as f:
        cpp = f.read()

    if TAB_FOCUS_GUARD_MARKER in cpp:
        print('history_inner_widget.cpp already has Tab focus guard v2')
        return

    changed = False
    cpp, c1 = patch_event_hook(cpp)
    changed = changed or c1
    cpp, c2 = patch_key_press_event(cpp)
    changed = changed or c2

    if changed:
        with open(CPP_PATH, 'w', encoding='utf-8') as f:
            f.write(cpp)
        print('history_inner_widget.cpp patched (Tab playback guard)')
    else:
        print('No Tab playback guard changes applied')


if __name__ == '__main__':
    main()
