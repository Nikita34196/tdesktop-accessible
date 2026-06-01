#!/usr/bin/env python3
"""Patch keyboard.h for shared-media list NVDA arrow speech."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATH = ROOT / "accessibility" / "telegram_accessibility_keyboard.h"

HELPERS = '''
inline bool IsSharedMediaInnerPanel(QWidget *w) {
    return w && DynamicTypeName(w) == QLatin1String("Info::Media::InnerWidget");
}

// Shared-media panel: focus often sits on InnerWidget while arrow
// handling and NVDA speech live on ListWidget (workflow patch 7n).
inline QWidget *FindSharedMediaListWidget(QWidget *from) {
    if (!from) {
        return nullptr;
    }
    if (IsSharedMediaListPanel(from)) {
        return from;
    }
    for (QWidget *w = from; w; w = w->parentWidget()) {
        if (QWidget *list = FindByType(w, "Info::Media::ListWidget")) {
            return list;
        }
        if (QWidget *list = FindByTypeAny(w, "Info::Media::ListWidget")) {
            return list;
        }
    }
    if (QWidget *root = from->window()) {
        return FindByTypeAny(root, "Info::Media::ListWidget");
    }
    return nullptr;
}

inline void AnnounceSharedMediaListRow(QWidget *list) {
    if (!list) {
        return;
    }
    QMetaObject::invokeMethod(
        list,
        "a11yAnnounceCurrentRow",
        Qt::DirectConnection);
}

inline void FocusSharedMediaList(QWidget *root) {
    QWidget *list = FindSharedMediaListWidget(root);
    if (!list) {
        list = FindByTypeAny(root, "Info::Media::ListWidget");
    }
    if (!list) {
        return;
    }
    if (list->focusPolicy() == Qt::NoFocus) {
        list->setFocusPolicy(Qt::StrongFocus);
    }
    list->setFocus(Qt::ShortcutFocusReason);
    QAccessibleEvent focusEv(list, QAccessible::Focus);
    QAccessible::updateAccessibility(&focusEv);
    AnnounceSharedMediaListRow(list);
}

'''

ARROW_COND_OLD = '''                if (detail::IsChatListPanel(focused)
                    || detail::IsMessageListPanel(focused)
                    || detail::IsSharedMediaListPanel(focused)) {'''

ARROW_COND_NEW = '''                if (detail::IsChatListPanel(focused)
                    || detail::IsMessageListPanel(focused)
                    || detail::IsSharedMediaListPanel(focused)
                    || detail::IsSharedMediaInnerPanel(focused)) {'''

ARROW_TAIL_OLD = '''                        LogLine(summary);
                    });
                }
            }
        }
        // Ctrl+Shift+R — voice-record shortcut for screen reader users.'''

ARROW_TAIL_NEW = '''                        LogLine(summary);
                    });
                    // Files panel: focus is often on InnerWidget, not
                    // ListWidget — patch 7n keyPressEvent may not run.
                    if (detail::IsSharedMediaInnerPanel(focused)) {
                        QPointer<QWidget> inner(focused);
                        QTimer::singleShot(80, [inner] {
                            if (!inner) {
                                return;
                            }
                            detail::AnnounceSharedMediaListRow(
                                detail::FindSharedMediaListWidget(
                                    inner.data()));
                        });
                    }
                }
            }
        }
        // Ctrl+Shift+R — voice-record shortcut for screen reader users.'''

CTRL_F_OLD = '''                    LogLine(QStringLiteral(
                        "Ctrl+Shift+F -> TopBar.accessibilityShowSharedMediaFiles"
                        " invoked=%1").arg(ok ? 1 : 0));
                    return true;'''

CTRL_F_NEW = '''                    LogLine(QStringLiteral(
                        "Ctrl+Shift+F -> TopBar.accessibilityShowSharedMediaFiles"
                        " invoked=%1").arg(ok ? 1 : 0));
                    QPointer<QWidget> rootPtr(root);
                    QTimer::singleShot(450, [rootPtr] {
                        if (!rootPtr) {
                            return;
                        }
                        detail::FocusSharedMediaList(rootPtr.data());
                    });
                    return true;'''


def main() -> int:
    src = PATH.read_text(encoding="utf-8")
    orig = src

    anchor = '''inline bool IsSharedMediaListPanel(QWidget *w) {
    return w && DynamicTypeName(w) == QLatin1String("Info::Media::ListWidget");
}

inline QWidget *FindTopBar(QWidget *root) {'''

    if "FindSharedMediaListWidget" not in src:
        if anchor not in src:
            print("ERROR: IsSharedMediaListPanel anchor not found", file=sys.stderr)
            return 1
        src = src.replace(anchor, anchor.replace(
            "inline QWidget *FindTopBar",
            HELPERS + "inline QWidget *FindTopBar",
            1))

    if ARROW_COND_OLD in src:
        src = src.replace(ARROW_COND_OLD, ARROW_COND_NEW, 1)
    elif ARROW_COND_NEW not in src:
        print("ERROR: arrow-key condition block not found", file=sys.stderr)
        return 1

    if ARROW_TAIL_OLD in src:
        src = src.replace(ARROW_TAIL_OLD, ARROW_TAIL_NEW, 1)
    elif "AnnounceSharedMediaListRow" not in src:
        print("ERROR: arrow-key tail block not found", file=sys.stderr)
        return 1

    if CTRL_F_OLD in src:
        src = src.replace(CTRL_F_OLD, CTRL_F_NEW, 1)
    elif "FocusSharedMediaList" not in src:
        print("ERROR: Ctrl+Shift+F block not found", file=sys.stderr)
        return 1

    if src == orig:
        print("keyboard.h already patched for media NVDA")
        return 0

    PATH.write_text(src, encoding="utf-8")
    print("keyboard.h patched for media list NVDA speech")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
