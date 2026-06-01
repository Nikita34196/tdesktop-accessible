#!/usr/bin/env python3
"""Patch accessibility sources for extended a11y features."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KB = ROOT / "accessibility" / "telegram_accessibility_keyboard.h"
NAMES = ROOT / "accessibility" / "telegram_accessibility_names.h"

HELPERS = r'''
inline QWidget *FindHistoryWidget(QWidget *root) {
    return FindByType(root, "HistoryWidget");
}

inline QWidget *FindDialogsSearch(QWidget *root) {
    if (QWidget *dialogs = FindByType(root, "Dialogs::Widget")) {
        if (QWidget *field = FindByType(dialogs, "Ui::InputField")) {
            if (field->isVisible()) {
                return field;
            }
        }
    }
    return nullptr;
}

inline QWidget *FindSharedMediaList(QWidget *root) {
    return FindByTypeAny(root, "Info::Media::ListWidget");
}

inline QWidget *FindProfileInner(QWidget *root) {
    return FindByTypeAny(root, "Info::Profile::InnerWidget");
}

inline QWidget *FindGroupCallBar(QWidget *root) {
    if (QWidget *history = FindHistoryWidget(root)) {
        if (QWidget *bar = FindByType(history, "GroupCallBar")) {
            return bar;
        }
        if (QWidget *bar = FindByType(history, "Ui::GroupCallBar")) {
            return bar;
        }
    }
    return FindByTypeAny(root, "GroupCallBar");
}

inline QWidget *FindTabbedSelector(QWidget *root) {
    return FindByTypeAny(root, "TabbedSelector");
}

inline QWidget *FindEmojiButton(QWidget *root) {
    return FindByTypeAny(root, "EmojiButton");
}

'''

DISCOVER_APPEND = r'''
        // 5) Shared media (files / links / photos) in the info column.
        if (QWidget *media = FindSharedMediaList(root)) {
            if (media->isVisible()) {
                const auto mediaName = media->accessibleName().simplified();
                out.append({ media,
                    mediaName.isEmpty()
                        ? QStringLiteral("Медиа чата")
                        : mediaName });
            }
        }

        // 6) Chat profile when the info panel is open.
        if (QWidget *profile = FindProfileInner(root)) {
            if (profile->isVisible()) {
                out.append({ profile, QStringLiteral("Профиль чата") });
            }
        }

        // 7) Active group voice/video call bar.
        if (QWidget *callBar = FindGroupCallBar(root)) {
            if (callBar->isVisible()) {
                out.append({ callBar, QStringLiteral("Групповой звонок") });
            }
        }

        // 8) Global search in the chat list column.
        if (QWidget *search = FindDialogsSearch(root)) {
            if (search->isVisible()) {
                out.append({ search, QStringLiteral("Поиск чатов") });
            }
        }

'''

HOTKEYS = r'''
        if (key == Qt::Key_P
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *topBar = detail::FindTopBar(root)) {
                    const bool ok = QMetaObject::invokeMethod(
                        topBar, "accessibilityShowSharedMediaPhotos",
                        Qt::DirectConnection);
                    LogLine(QStringLiteral(
                        "Ctrl+Shift+P -> TopBar.accessibilityShowSharedMediaPhotos"
                        " invoked=%1").arg(ok ? 1 : 0));
                    return true;
                }
            }
        }
        if (key == Qt::Key_G
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *search = detail::FindDialogsSearch(root)) {
                    focusAndAnnounce(search, QStringLiteral("Поиск чатов"));
                    return true;
                }
            }
            nvda::Speak(QStringLiteral("Поле поиска чатов не найдено"));
            return true;
        }
        if (key == Qt::Key_E
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *topBar = detail::FindTopBar(root)) {
                    const bool ok = QMetaObject::invokeMethod(
                        topBar, "accessibilityOpenChatSearch",
                        Qt::DirectConnection);
                    LogLine(QStringLiteral(
                        "Ctrl+Shift+E -> TopBar.accessibilityOpenChatSearch"
                        " invoked=%1").arg(ok ? 1 : 0));
                    return true;
                }
            }
            return true;
        }
        if (key == Qt::Key_M
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *history = detail::FindHistoryWidget(root)) {
                    const bool ok = QMetaObject::invokeMethod(
                        history, "accessibilityOpenPinnedMessages",
                        Qt::DirectConnection);
                    LogLine(QStringLiteral(
                        "Ctrl+Shift+M -> HistoryWidget.accessibilityOpenPinnedMessages"
                        " invoked=%1").arg(ok ? 1 : 0));
                    return true;
                }
            }
            return true;
        }
        if (key == Qt::Key_Y
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *topBar = detail::FindTopBar(root)) {
                    const bool ok = QMetaObject::invokeMethod(
                        topBar, "accessibilitySpeakChatStatus",
                        Qt::DirectConnection);
                    LogLine(QStringLiteral(
                        "Ctrl+Shift+Y -> TopBar.accessibilitySpeakChatStatus"
                        " invoked=%1").arg(ok ? 1 : 0));
                    return true;
                }
            }
            return true;
        }
        if (key == Qt::Key_K
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *history = detail::FindHistoryWidget(root)) {
                    const bool ok = QMetaObject::invokeMethod(
                        history, "accessibilityToggleStickers",
                        Qt::DirectConnection);
                    if (ok) {
                        LogLine(QStringLiteral(
                            "Ctrl+Shift+K -> HistoryWidget.accessibilityToggleStickers"));
                        return true;
                    }
                }
                if (QWidget *emoji = detail::FindEmojiButton(root)) {
                    if (QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(emoji)) {
                        iface->actionInterface()->doAction(QAccessibleActionInterface::PressAction);
                    } else {
                        emoji->click();
                    }
                    nvda::Speak(QStringLiteral("Эмодзи и стикеры"));
                    return true;
                }
            }
            return true;
        }
        if (key == Qt::Key_C
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                if (QWidget *callBar = detail::FindGroupCallBar(root)) {
                    if (callBar->isVisible()) {
                        focusAndAnnounce(callBar, QStringLiteral("Групповой звонок"));
                        return true;
                    }
                }
                if (QWidget *topBar = detail::FindTopBar(root)) {
                    const bool ok = QMetaObject::invokeMethod(
                        topBar, "groupCall",
                        Qt::DirectConnection);
                    LogLine(QStringLiteral(
                        "Ctrl+Shift+C -> TopBar.groupCall invoked=%1")
                        .arg(ok ? 1 : 0));
                    return true;
                }
            }
            nvda::Speak(QStringLiteral("Групповой звонок недоступен"));
            return true;
        }
'''

ARROW_MEDIA = r'''                                if (type == QLatin1String("HistoryInner")) {
                                    if (!detail::IsUselessListItemName(name)) {
                                        nvda::SpeakMessage(name);
                                    }
                                } else if (detail::IsSharedMediaListPanel(alive.data())) {
                                    if (!detail::IsUselessListItemName(name)) {
                                        nvda::SpeakForced(name);
                                    }
                                }'''

def patch_keyboard():
    text = KB.read_text(encoding="utf-8")
    if "FindDialogsSearch" not in text:
        anchor = "} // namespace detail\n\nclass KeyboardNavigationFilter"
        if anchor not in text:
            raise SystemExit("keyboard.h: detail namespace anchor missing")
        text = text.replace(
            "} // namespace detail\n\nclass KeyboardNavigationFilter",
            HELPERS + "} // namespace detail\n\nclass KeyboardNavigationFilter",
            1,
        )
    if "accessibilityShowSharedMediaPhotos" not in text:
        text = text.replace(
            "                return QObject::eventFilter(obj, event);",
            HOTKEYS + "\n        return QObject::eventFilter(obj, event);",
            1,
        )
    if "Shared media (files" not in text:
        text = text.replace(
            "        return out;\n    }\n\n    void focusAndAnnounce",
            DISCOVER_APPEND + "        return out;\n    }\n\n    void focusAndAnnounce",
            1,
        )
    old_arrow = (
        "                                if (type == QLatin1String(\"HistoryInner\")) {\n"
        "                                    if (!detail::IsUselessListItemName(name)) {\n"
        "                                        nvda::SpeakMessage(name);\n"
        "                                    }\n"
        "                                }"
    )
    if "IsSharedMediaListPanel(alive" not in text and old_arrow in text:
        text = text.replace(old_arrow, ARROW_MEDIA, 1)
    KB.write_text(text, encoding="utf-8")
    print("keyboard.h patched")


def patch_names():
    text = NAMES.read_text(encoding="utf-8")
    extra_rules = """
        { \"PinnedBar\",        nullptr,            nullptr, \"Закреплённые сообщения\" },
        { \"GroupCallBar\",     \"GroupCallBar\",   nullptr, \"Групповой звонок\" },
        { \"TabbedSelector\",   \"TabbedSelector\", nullptr, \"Стикеры и эмодзи\" },
        { \"ComposeSearch\",    \"ComposeSearch\",  nullptr, \"Поиск в чате\" },
        { \"SearchField\",      \"SearchField\",    nullptr, \"Поиск в чате\" },
        { \"ListWidget\",       \"ListWidget\",     nullptr, \"Список медиа\" },
"""
    if "PinnedBar" not in text:
        text = text.replace(
            '        { "MuteButton",       "MuteButton",       nullptr, "Отключить звук" },',
            '        { "MuteButton",       "MuteButton",       nullptr, "Отключить звук" },'
            + extra_rules,
            1,
        )
    NAMES.write_text(text, encoding="utf-8")
    print("names.h patched")


if __name__ == "__main__":
    patch_keyboard()
    patch_names()
