#!/usr/bin/env python3
"""Shared media ListWidget: Enter, Tab, context menu (patches 7n-c / 7n-d)."""
from __future__ import annotations

import os
import pathlib
import re
import sys

ROOT = os.environ.get('REPO_NAME', 'tdesktop')
MARKER_V1 = "a11y-media-list-actions-v1"
MARKER_V2 = "a11y-media-list-context-v2"

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

// a11y-media-list-context-v2
void ListWidget::a11yShowContextMenu() {
	a11yEnsureFocusedIndex();
	if (_a11yCurrentIndex < 0 || _a11yCurrentIndex >= int(_a11yFlatItems.size())) {
		return;
	}
	const auto &flat = _a11yFlatItems[_a11yCurrentIndex];
	const auto rect = flat.rect;
	const auto localPoint = rect.center();
	const auto globalPos = mapToGlobal(localPoint);

	// mouseActionUpdate() bails out when the scroll viewport is unset.
	if (_visibleBottom <= _visibleTop && height() > 0) {
		_visibleTop = 0;
		_visibleBottom = height();
	}

	if (focusPolicy() == Qt::NoFocus) {
		setFocusPolicy(Qt::StrongFocus);
	}
	setFocus(Qt::PopupFocusReason);
	mouseActionUpdate(globalPos);

	if (!_overState.item || !_overState.inside) {
		_overState.item = const_cast<HistoryItem*>(flat.item.get());
		_overState.size = rect.size();
		_overState.cursor = localPoint - rect.topLeft();
		_overState.inside = true;
		_overLayout = flat.layout;
	}

	// Mouse reason runs mouseActionUpdate() inside showContextMenu().
	QContextMenuEvent event(
		QContextMenuEvent::Mouse,
		mapFromGlobal(globalPos),
		globalPos);
	showContextMenu(&event, ContextMenuSource::Other);
}
'''

LIST_CONTEXT_MENU_EVENT = r'''void ListWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() == QContextMenuEvent::Keyboard
			|| e->reason() == QContextMenuEvent::Other) {
		a11yShowContextMenu();
		e->accept();
		return;
	}
	showContextMenu(
		e,
		(e->reason() == QContextMenuEvent::Mouse)
			? ContextMenuSource::Mouse
			: ContextMenuSource::Other);
}
'''

INNER_EVENT_HOOK_H = "\tbool eventHook(QEvent *e) override;\n"
INNER_CONTEXT_MENU_H = "\tvoid contextMenuEvent(QContextMenuEvent *e) override;\n"

INNER_EVENT_HOOK_CPP = r'''
bool InnerWidget::eventHook(QEvent *e) {
	// a11y-media-list-context-v2: context menu often arrives as QEvent::ContextMenu.
	if (_list && e->type() == QEvent::ContextMenu) {
		_list->a11yShowContextMenu();
		e->accept();
		return true;
	}
	// a11y-media-list-actions-v1: trap Tab/Enter/context keys before Qt focus chain.
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

void InnerWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (_list) {
		_list->a11yShowContextMenu();
		e->accept();
		return;
	}
	RpWidget::contextMenuEvent(e);
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


def ensure_includes(cpp: str) -> str:
    if "#include <QContextMenuEvent>" not in cpp:
        cpp = cpp.replace(
            "#include <QKeyEvent>\n",
            "#include <QContextMenuEvent>\n#include <QKeyEvent>\n",
            1,
        )
    return cpp


def patch_list_cpp(cpp: str) -> tuple[str, bool]:
    changed = False
    cpp = ensure_includes(cpp)

    if "ListWidget::a11yProcessKey" not in cpp:
        print("WARNING: ListWidget::a11yProcessKey missing — skip cpp", file=sys.stderr)
        return cpp, False

    if MARKER_V1 not in cpp:
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
        changed = True
    elif MARKER_V2 not in cpp:
        cpp, n = re.subn(
            r"void ListWidget::a11yShowContextMenu\(\) \{.*?\n\}\n",
            (
                "// a11y-media-list-context-v2\n"
                "void ListWidget::a11yShowContextMenu() {\n"
                "\ta11yEnsureFocusedIndex();\n"
                "\tif (_a11yCurrentIndex < 0 || _a11yCurrentIndex >= int(_a11yFlatItems.size())) {\n"
                "\t\treturn;\n"
                "\t}\n"
                "\tconst auto &flat = _a11yFlatItems[_a11yCurrentIndex];\n"
                "\tconst auto rect = flat.rect;\n"
                "\tconst auto localPoint = rect.center();\n"
                "\tconst auto globalPos = mapToGlobal(localPoint);\n"
                "\n"
                "\tif (_visibleBottom <= _visibleTop && height() > 0) {\n"
                "\t\t_visibleTop = 0;\n"
                "\t\t_visibleBottom = height();\n"
                "\t}\n"
                "\n"
                "\tif (focusPolicy() == Qt::NoFocus) {\n"
                "\t\tsetFocusPolicy(Qt::StrongFocus);\n"
                "\t}\n"
                "\tsetFocus(Qt::PopupFocusReason);\n"
                "\tmouseActionUpdate(globalPos);\n"
                "\n"
                "\tif (!_overState.item || !_overState.inside) {\n"
                "\t\t_overState.item = const_cast<HistoryItem*>(flat.item.get());\n"
                "\t\t_overState.size = rect.size();\n"
                "\t\t_overState.cursor = localPoint - rect.topLeft();\n"
                "\t\t_overState.inside = true;\n"
                "\t\t_overLayout = flat.layout;\n"
                "\t}\n"
                "\n"
                "\tQContextMenuEvent event(\n"
                "\t\tQContextMenuEvent::Mouse,\n"
                "\t\tmapFromGlobal(globalPos),\n"
                "\t\tglobalPos);\n"
                "\tshowContextMenu(&event, ContextMenuSource::Other);\n"
                "}\n"
            ),
            cpp,
            count=1,
            flags=re.DOTALL,
        )
        if n == 0:
            print("ERROR: could not upgrade ListWidget::a11yShowContextMenu", file=sys.stderr)
            sys.exit(1)
        changed = True

    if "a11yShowContextMenu();\n\t\te->accept();\n\t\treturn;\n\t}\n\tshowContextMenu(" not in cpp:
        cpp, n = re.subn(
            r"void ListWidget::contextMenuEvent\(QContextMenuEvent \*e\) \{\n"
            r"\tshowContextMenu\(\n"
            r"\t\te,\n"
            r"\t\t\(e->reason\(\) == QContextMenuEvent::Mouse\)\n"
            r"\t\t\t\? ContextMenuSource::Mouse\n"
            r"\t\t\t: ContextMenuSource::Other\);\n"
            r"\}\n",
            LIST_CONTEXT_MENU_EVENT + "\n",
            cpp,
            count=1,
        )
        if n == 0:
            print("ERROR: ListWidget::contextMenuEvent landmark not found", file=sys.stderr)
            sys.exit(1)
        changed = True

    return cpp, changed


def patch_inner_header(h: str) -> str:
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
    if "eventHook" not in h:
        h = h.replace(
            "void keyPressEvent(QKeyEvent *e) override;\n",
            "void keyPressEvent(QKeyEvent *e) override;\n" + INNER_EVENT_HOOK_H,
            1,
        )
    if "contextMenuEvent" not in h:
        anchor = "void keyPressEvent(QKeyEvent *e) override;\n"
        if "eventHook" in h:
            anchor = "bool eventHook(QEvent *e) override;\n"
        h = h.replace(anchor, anchor + INNER_CONTEXT_MENU_H, 1)
    return h


def patch_inner_cpp(cpp: str) -> tuple[str, bool]:
    if "InnerWidget::keyPressEvent" not in cpp and "InnerWidget::eventHook" not in cpp:
        print("WARNING: InnerWidget key handlers missing — skip inner cpp", file=sys.stderr)
        return cpp, False

    if "#include <QEvent>" not in cpp:
        cpp = cpp.replace(
            "#include <QKeyEvent>\n",
            "#include <QEvent>\n#include <QKeyEvent>\n",
            1,
        )
    if "#include <QContextMenuEvent>" not in cpp:
        cpp = cpp.replace(
            "#include <QKeyEvent>\n",
            "#include <QContextMenuEvent>\n#include <QKeyEvent>\n",
            1,
        )

    if MARKER_V2 in cpp:
        return cpp, False

    old_hook = (
        "bool InnerWidget::eventHook(QEvent *e) {\n"
        "\t// a11y-media-list-actions-v1: trap Tab/Enter/context before Qt focus chain.\n"
        "\tif (e->type() == QEvent::KeyPress && _list) {\n"
    )
    if old_hook in cpp:
        cpp = re.sub(
            r"bool InnerWidget::eventHook\(QEvent \*e\) \{.*?\n\}\n",
            INNER_EVENT_HOOK_CPP.strip() + "\n",
            cpp,
            count=1,
            flags=re.DOTALL,
        )
        if "InnerWidget::contextMenuEvent" not in cpp:
            anchor = "\n} // namespace Media\n} // namespace Info"
            cpp = cpp.replace(anchor, INNER_EVENT_HOOK_CPP + anchor, 1)
        return cpp, True

    if "InnerWidget::eventHook" not in cpp:
        anchor = "\n} // namespace Media\n} // namespace Info"
        if anchor not in cpp:
            print("ERROR: inner_widget namespace close not found", file=sys.stderr)
            sys.exit(1)
        cpp = cpp.replace(anchor, INNER_EVENT_HOOK_CPP + anchor, 1)
        return cpp, True

    return cpp, False


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

    h = patch_list_header(h)
    cpp, list_changed = patch_list_cpp(cpp)
    inner_h = patch_inner_header(inner_h)
    inner_cpp, inner_changed = patch_inner_cpp(inner_cpp)

    if not list_changed and not inner_changed:
        if MARKER_V2 in cpp:
            print("info_media_list_widget: context menu v2 already patched")
        else:
            print("info_media_list_widget.cpp already has media list actions")
        return 0

    H_PATH.write_text(h, encoding="utf-8")
    CPP_PATH.write_text(cpp, encoding="utf-8")
    INNER_H.write_text(inner_h, encoding="utf-8")
    INNER_CPP.write_text(inner_cpp, encoding="utf-8")
    print("info_media_list_widget: context menu v2 patched (7n-d)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
