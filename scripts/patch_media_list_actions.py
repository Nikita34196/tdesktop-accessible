#!/usr/bin/env python3
"""Upgrade shared media ListWidget: Enter, Tab, context menu (patch 7n-c)."""
from __future__ import annotations

import os
import pathlib
import re
import sys

ROOT = os.environ.get('REPO_NAME', 'tdesktop')
MARKER = "a11y-media-list-actions-v1"

H_PATH = pathlib.Path(ROOT) / "Telegram/SourceFiles/info/media/info_media_list_widget.h"
CPP_PATH = pathlib.Path(ROOT) / "Telegram/SourceFiles/info/media/info_media_list_widget.cpp"
INNER_H = pathlib.Path(ROOT) / "Telegram/SourceFiles/info/media/info_media_inner_widget.h"
INNER_CPP = pathlib.Path(ROOT) / "Telegram/SourceFiles/info/media/info_media_inner_widget.cpp"

HEADER_DECLS = (
    "\n\tQ_INVOKABLE void a11yMoveFocusedRow(int delta);\n"
    "\tQ_INVOKABLE void a11yShowContextMenu();\n"
    "\tvoid a11yEnsureFocusedIndex();\n"
)

A11Y_PROCESS_KEY = r'''bool ListWidget::a11yProcessKey(QKeyEvent *e) {
	const auto key = e->key();
	const auto mods = e->modifiers();
	if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
			return false;
		}
		a11yEnsureFocusedIndex();
		a11yActivateFocused();
		return true;
	}
	if (key == Qt::Key_Menu
			|| (key == Qt::Key_F10 && (mods & Qt::ShiftModifier))) {
		a11yShowContextMenu();
		return true;
	}
	if (key == Qt::Key_Tab && !(mods & ~Qt::ShiftModifier)) {
		a11yMoveFocusedRow((mods & Qt::ShiftModifier) ? -1 : 1);
		return true;
	}
	const auto isDown = (key == Qt::Key_Down);
	const auto isUp = (key == Qt::Key_Up);
	const auto isPageDown = (key == Qt::Key_PageDown);
	const auto isPageUp = (key == Qt::Key_PageUp);
	const auto isHome = (key == Qt::Key_Home);
	const auto isEnd = (key == Qt::Key_End);
	if (!isDown && !isUp && !isPageDown && !isPageUp && !isHome && !isEnd) {
		return false;
	}
	if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
		return false;
	}
	a11yRecomputeItems();
	const int total = int(_a11yFlatItems.size());
	if (total <= 0) {
		return false;
	}
	const int prev = _a11yCurrentIndex;
	int next = prev;
	if (isHome) {
		next = 0;
	} else if (isEnd) {
		next = total - 1;
	} else if (isDown) {
		next = (prev < 0) ? 0 : std::min(prev + 1, total - 1);
	} else if (isUp) {
		next = (prev < 0) ? (total - 1) : std::max(prev - 1, 0);
	} else if (isPageDown) {
		next = (prev < 0) ? 0 : std::min(prev + 5, total - 1);
	} else if (isPageUp) {
		next = (prev < 0) ? (total - 1) : std::max(prev - 5, 0);
	}
	_a11yCurrentIndex = next;
	if (prev != next) {
		accessibilityChildFocused(next);
		a11yScrollToIndex(next);
		const auto label = accessibilityChildName(next);
		if (!label.isEmpty()) {
			a11yAnnounceCurrentRow();
		}
	}
	return true;
}
'''

HELPER_METHODS = r'''
// a11y-media-list-actions-v1
void ListWidget::a11yEnsureFocusedIndex() {
	a11yRecomputeItems();
	const int total = int(_a11yFlatItems.size());
	if (total <= 0) {
		_a11yCurrentIndex = -1;
		return;
	}
	if (_a11yCurrentIndex < 0 || _a11yCurrentIndex >= total) {
		_a11yCurrentIndex = 0;
		accessibilityChildFocused(_a11yCurrentIndex);
		a11yScrollToIndex(_a11yCurrentIndex);
	}
}

void ListWidget::a11yMoveFocusedRow(int delta) {
	if (delta == 0) {
		return;
	}
	a11yRecomputeItems();
	const int total = int(_a11yFlatItems.size());
	if (total <= 0) {
		return;
	}
	const int prev = (_a11yCurrentIndex < 0) ? 0 : _a11yCurrentIndex;
	int next = prev + delta;
	if (next < 0) {
		next = total - 1;
	} else if (next >= total) {
		next = 0;
	}
	_a11yCurrentIndex = next;
	if (prev != next) {
		accessibilityChildFocused(next);
		a11yScrollToIndex(next);
		const auto label = accessibilityChildName(next);
		if (!label.isEmpty()) {
			a11yAnnounceCurrentRow();
		}
	}
}

void ListWidget::a11yShowContextMenu() {
	a11yEnsureFocusedIndex();
	if (_a11yCurrentIndex < 0 || _a11yCurrentIndex >= int(_a11yFlatItems.size())) {
		return;
	}
	const auto &flat = _a11yFlatItems[_a11yCurrentIndex];
	const auto item = flat.item;
	const auto rect = flat.rect;
	_overState.item = const_cast<HistoryItem*>(item.get());
	_overState.size = rect.size();
	_overState.cursor = rect.center();
	_overState.inside = true;
	_overLayout = flat.layout;
	const auto globalPos = mapToGlobal(rect.center());
	QContextMenuEvent event(
		QContextMenuEvent::Keyboard,
		rect.center(),
		globalPos);
	showContextMenu(&event, ContextMenuSource::Other);
}
'''

INNER_EVENT_HOOK_H = "\tbool eventHook(QEvent *e) override;\n"

INNER_EVENT_HOOK_CPP = r'''
bool InnerWidget::eventHook(QEvent *e) {
	// a11y-media-list-actions-v1: trap Tab/Enter/context before Qt focus chain.
	if (e->type() == QEvent::KeyPress && _list) {
		auto *key = static_cast<QKeyEvent*>(e);
		const auto k = key->key();
		const auto mods = key->modifiers();
		const auto isTab = (k == Qt::Key_Tab) && !(mods & ~Qt::ShiftModifier);
		const auto isEnter = (k == Qt::Key_Return || k == Qt::Key_Enter)
			&& !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
		const auto isMenu = (k == Qt::Key_Menu)
			|| (k == Qt::Key_F10 && (mods & Qt::ShiftModifier));
		if (isTab || isEnter || isMenu) {
			if (_list->a11yProcessKey(key)) {
				key->accept();
				return true;
			}
		}
	}
	return RpWidget::eventHook(e);
}
'''


def patch_list_header(h: str) -> str:
    if "a11yMoveFocusedRow" in h:
        return h
    if "a11yActivateFocused" not in h:
        print("WARNING: ListWidget a11yActivateFocused missing — skip header", file=sys.stderr)
        return h
    return h.replace(
        "\tQ_INVOKABLE void a11yActivateFocused();\n",
        "\tQ_INVOKABLE void a11yActivateFocused();\n" + HEADER_DECLS,
        1,
    )


def patch_list_cpp(cpp: str) -> str:
    if MARKER in cpp:
        return cpp
    if "ListWidget::a11yProcessKey" not in cpp:
        print("WARNING: ListWidget::a11yProcessKey missing — skip cpp", file=sys.stderr)
        return cpp

    if "#include <QContextMenuEvent>" not in cpp:
        cpp = cpp.replace(
            "#include <QKeyEvent>\n",
            "#include <QContextMenuEvent>\n#include <QKeyEvent>\n",
            1,
        )

    cpp, n = re.subn(
        r"bool ListWidget::a11yProcessKey\(QKeyEvent \*e\) \{.*?\n\}\n",
        A11Y_PROCESS_KEY + "\n",
        cpp,
        count=1,
        flags=re.DOTALL,
    )
    if n == 0:
        print("ERROR: could not replace ListWidget::a11yProcessKey", file=sys.stderr)
        sys.exit(1)

    anchor = "void ListWidget::a11yActivateFocused() {"
    if anchor not in cpp:
        print("ERROR: a11yActivateFocused anchor missing", file=sys.stderr)
        sys.exit(1)
    cpp = cpp.replace(anchor, HELPER_METHODS + "\n" + anchor, 1)
    return cpp


def patch_inner_header(h: str) -> str:
    if "eventHook" in h:
        return h
    if "keyPressEvent" not in h:
        h, n = re.subn(
            r"(protected:\s*\n)",
            r"\1\tvoid keyPressEvent(QKeyEvent *e) override;\n\n",
            h,
            count=1,
        )
        if n == 0:
            print("WARNING: InnerWidget protected: not found", file=sys.stderr)
            return h
    return h.replace(
        "void keyPressEvent(QKeyEvent *e) override;\n",
        "void keyPressEvent(QKeyEvent *e) override;\n" + INNER_EVENT_HOOK_H,
        1,
    )


def patch_inner_cpp(cpp: str) -> str:
    if MARKER in cpp:
        return cpp
    if "InnerWidget::keyPressEvent" not in cpp:
        print("WARNING: InnerWidget::keyPressEvent missing — skip inner cpp", file=sys.stderr)
        return cpp
    if "#include <QEvent>" not in cpp:
        cpp = cpp.replace(
            "#include <QKeyEvent>\n",
            "#include <QEvent>\n#include <QKeyEvent>\n",
            1,
        )
    anchor = "\n} // namespace Media\n} // namespace Info"
    if anchor not in cpp:
        print("ERROR: inner_widget namespace close not found", file=sys.stderr)
        sys.exit(1)
    return cpp.replace(anchor, INNER_EVENT_HOOK_CPP + anchor, 1)


def main() -> int:
    missing = [p for p in (H_PATH, CPP_PATH, INNER_H, INNER_CPP) if not p.exists()]
    if missing:
        for p in missing:
            print(f"WARNING: missing {p} — skip media list actions patch")
        return 0

    h = H_PATH.read_text(encoding="utf-8")
    cpp = CPP_PATH.read_text(encoding="utf-8")
    inner_h = INNER_H.read_text(encoding="utf-8")
    inner_cpp = INNER_CPP.read_text(encoding="utf-8")

    orig_cpp = cpp
    h = patch_list_header(h)
    cpp = patch_list_cpp(cpp)
    inner_h = patch_inner_header(inner_h)
    inner_cpp = patch_inner_cpp(inner_cpp)

    if cpp == orig_cpp and MARKER in orig_cpp:
        print("info_media_list_widget.cpp already has media list actions")
        return 0

    H_PATH.write_text(h, encoding="utf-8")
    CPP_PATH.write_text(cpp, encoding="utf-8")
    INNER_H.write_text(inner_h, encoding="utf-8")
    INNER_CPP.write_text(inner_cpp, encoding="utf-8")
    print("info_media_list_widget: Enter/Tab/context menu patched (7n-c)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
