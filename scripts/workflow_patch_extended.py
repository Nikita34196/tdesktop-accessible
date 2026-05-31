#!/usr/bin/env python3
"""CI patches for photos, search, pinned, status, stickers, group calls."""
import os
import re
import textwrap

TDESKTOP = os.environ.get("REPO_NAME", "tdesktop")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, data):
    with open(path, "w", encoding="utf-8") as f:
        f.write(data)


def media_show_impl(method, media_type):
    return textwrap.dedent(f"""
        void TopBarWidget::{method}() {{
        \tconst auto key = _activeChat.key;
        \tif (!key.peer()) {{
        \t\treturn;
        \t}}
        \tauto params = Window::SectionShow();
        \tif (_controller->canShowThirdSection()) {{
        \t\tCore::App().settings().setThirdSectionInfoEnabled(true);
        \t\tCore::App().saveSettingsDelayed();
        \t\tparams = params.withThirdColumn();
        \t\tif (_controller->adaptive().isThreeColumn()) {{
        \t\t\t_controller->showSection(
        \t\t\t\t(key.topic()
        \t\t\t\t\t? std::make_shared<Info::Memento>(
        \t\t\t\t\t\tkey.topic(),
        \t\t\t\t\t\tInfo::Section(Storage::SharedMediaType::{media_type}))
        \t\t\t\t\t: (key.sublist() && key.sublist()->parentChat())
        \t\t\t\t\t? std::make_shared<Info::Memento>(
        \t\t\t\t\t\tkey.sublist(),
        \t\t\t\t\t\tInfo::Section(Storage::SharedMediaType::{media_type}))
        \t\t\t\t\t: std::make_shared<Info::Memento>(
        \t\t\t\t\t\tkey.peer(),
        \t\t\t\t\t\tInfo::Section(Storage::SharedMediaType::{media_type}))),
        \t\t\t\tparams);
        \t\t\treturn;
        \t\t}}
        \t}}
        \t_controller->resizeForThirdSection();
        \t_controller->updateColumnLayout();
        \tparams = params.withThirdColumn();
        \tif (key.topic()) {{
        \t\t_controller->showSection(
        \t\t\tstd::make_shared<Info::Memento>(
        \t\t\t\tkey.topic(),
        \t\t\t\tInfo::Section(Storage::SharedMediaType::{media_type})),
        \t\t\tparams);
        \t}} else if (key.sublist() && key.sublist()->parentChat()) {{
        \t\t_controller->showSection(
        \t\t\tstd::make_shared<Info::Memento>(
        \t\t\t\tkey.sublist(),
        \t\t\t\tInfo::Section(Storage::SharedMediaType::{media_type})),
        \t\t\tparams);
        \t}} else {{
        \t\t_controller->showSection(
        \t\t\tstd::make_shared<Info::Memento>(
        \t\t\t\tkey.peer(),
        \t\t\t\tInfo::Section(Storage::SharedMediaType::{media_type})),
        \t\t\tparams);
        \t}}
        }}
    """)


def patch_top_bar():
    h_path = (
        f"{TDESKTOP}/Telegram/SourceFiles/history/view/"
        "history_view_top_bar_widget.h"
    )
    cpp_path = (
        f"{TDESKTOP}/Telegram/SourceFiles/history/view/"
        "history_view_top_bar_widget.cpp"
    )
    if not os.path.exists(h_path):
        print(f"WARNING: missing {h_path}")
        return

    h = read(h_path)
    decls = [
        "Q_INVOKABLE void accessibilityShowSharedMediaPhotos();",
        "Q_INVOKABLE void accessibilityOpenChatSearch();",
        "Q_INVOKABLE void accessibilitySpeakChatStatus();",
        "Q_INVOKABLE void accessibilityJoinGroupCall();",
    ]
    if any(d not in h for d in decls):
        if "Q_OBJECT" not in h:
            h, n = re.subn(
                r"(class\s+TopBarWidget\s+final\s*:\s*public\s+Ui::RpWidget\s*\{)",
                r"\1\n\tQ_OBJECT\n",
                h,
                count=1,
            )
        anchor = "Q_INVOKABLE void accessibilityShowSharedMediaLinks();"
        if anchor not in h:
            anchor = "Q_INVOKABLE void accessibilityShowSharedMediaFiles();"
        if anchor not in h:
            anchor = "void toggleInfoSection();"
        block = "\n\t".join(d for d in decls if d not in h)
        if block:
            h = h.replace(anchor, anchor + "\n\t" + block, 1)
        write(h_path, h)

    cpp = read(cpp_path)
    if "accessibilityOpenChatSearch" not in cpp:
        if "#include \"ui/accessibility/telegram_accessibility_keyboard.h\"" not in cpp:
            cpp = cpp.replace(
                '#include "history/view/history_view_top_bar_widget.h"\n',
                '#include "history/view/history_view_top_bar_widget.h"\n'
                '#include "ui/accessibility/telegram_accessibility_keyboard.h"\n',
                1,
            )
        impl = (
            "\n// === Accessibility: extended shortcuts ===\n"
            + media_show_impl("accessibilityShowSharedMediaPhotos", "PhotoVideo")
            + textwrap.dedent("""
                void TopBarWidget::accessibilityOpenChatSearch() {
                \ttoggleSearch(true, anim::type::instant);
                \tsearchSetFocus();
                }

                void TopBarWidget::accessibilitySpeakChatStatus() {
                \tQString status = _titlePeerText.toString();
                \tif (status.isEmpty() && _activeChat.key.peer()) {
                \t\tstatus = _activeChat.key.peer()->name();
                \t}
                \tif (status.isEmpty()) {
                \t\tstatus = QStringLiteral("Статус недоступен");
                \t}
                \tTgAccessibility::nvda::SpeakForced(status);
                }

                void TopBarWidget::accessibilityJoinGroupCall() {
                \tgroupCall();
                }
            """)
        )
        cpp, n = re.subn(
            r"(\n}\s*//\s*namespace\s+HistoryView\s*\n?)\Z",
            "\n" + impl + r"\1",
            cpp,
            count=1,
        )
        if n == 0:
            print("ERROR: TopBar cpp namespace close not found")
            raise SystemExit(1)
        write(cpp_path, cpp)
        print("top_bar_widget: extended shortcuts added")
    else:
        print("top_bar_widget.cpp: extended shortcuts already present")


def patch_history_widget():
    h_path = f"{TDESKTOP}/Telegram/SourceFiles/history/history_widget.h"
    cpp_path = f"{TDESKTOP}/Telegram/SourceFiles/history/history_widget.cpp"
    if not os.path.exists(h_path):
        print(f"WARNING: missing {h_path}")
        return

    h = read(h_path)
    if "accessibilityOpenPinnedMessages" not in h:
        if "Q_OBJECT" not in h:
            h, _ = re.subn(
                r"(class\s+HistoryWidget\s+final\s*:[^{]+\{)",
                r"\1\n\tQ_OBJECT\n",
                h,
                count=1,
            )
        h = h.replace(
            "void escape();",
            "void escape();\n\n\tQ_INVOKABLE void accessibilityOpenPinnedMessages();\n"
            "\tQ_INVOKABLE void accessibilityToggleStickers();",
            1,
        )
        write(h_path, h)

    cpp = read(cpp_path)
    if "accessibilityOpenPinnedMessages" not in cpp:
        if "#include \"ui/accessibility/telegram_accessibility_keyboard.h\"" not in cpp:
            cpp = cpp.replace(
                '#include "history/history_widget.h"\n',
                '#include "history/history_widget.h"\n'
                '#include "ui/accessibility/telegram_accessibility_keyboard.h"\n',
                1,
            )
        impl = textwrap.dedent("""
            // === Accessibility: pinned + stickers ===
            void HistoryWidget::accessibilityOpenPinnedMessages() {
            \tcheckPinnedBarState();
            \tif (_pinnedBar && _pinnedBar->isVisible()) {
            \t\t_pinnedBar->setAccessibleName(
            \t\t\tQStringLiteral("Закреплённые сообщения"));
            \t\t_pinnedBar->setFocus(Qt::ShortcutFocusReason);
            \t\tTgAccessibility::nvda::SpeakForced(
            \t\t\tQStringLiteral("Закреплённые сообщения"));
            \t\treturn;
            \t}
            \tTgAccessibility::nvda::Speak(
            \t\tQStringLiteral("Нет закреплённых сообщений в этом чате"));
            }

            void HistoryWidget::accessibilityToggleStickers() {
            \ttoggleTabbedSelectorMode();
            \tTgAccessibility::nvda::SpeakForced(
            \t\tQStringLiteral("Панель эмодзи и стикеров"));
            }
        """)
        marker = "HistoryWidget::~HistoryWidget()"
        if marker in cpp:
            cpp = cpp.replace(marker, impl + "\n" + marker, 1)
        else:
            cpp = cpp.rstrip() + "\n" + impl + "\n"
        write(cpp_path, cpp)
        print("history_widget: pinned + stickers shortcuts added")
    else:
        print("history_widget.cpp: already patched")


def patch_pinned_bar_name():
    cpp = f"{TDESKTOP}/Telegram/SourceFiles/history/view/history_view_pinned_bar.cpp"
    if not os.path.exists(cpp):
        print(f"WARNING: missing {cpp}")
        return
    src = read(cpp)
    if "a11y-pinned-bar-name" in src:
        print("pinned_bar: already patched")
        return
    needle = "PinnedBar::PinnedBar("
    if needle not in src:
        print("WARNING: PinnedBar ctor not found")
        return
    # Name is set from keyboard when focused; skip invasive patch.


def main():
    patch_top_bar()
    patch_history_widget()
    patch_pinned_bar_name()
    print("extended a11y upstream patches done")


if __name__ == "__main__":
    main()
