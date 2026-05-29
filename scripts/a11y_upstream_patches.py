#!/usr/bin/env python3
"""Apply upstream tdesktop patches for extended accessibility."""
import os
import re
import sys

ROOT = os.environ.get('TDESKTOP_ROOT', 'tdesktop')


def patch_top_bar():
    h = f'{ROOT}/Telegram/SourceFiles/history/view/history_view_top_bar_widget.h'
    cpp = f'{ROOT}/Telegram/SourceFiles/history/view/history_view_top_bar_widget.cpp'
    if not os.path.exists(h):
        print(f'WARNING: missing {h}')
        return

    with open(h, 'r', encoding='utf-8') as f:
        src = f.read()
    if 'a11ySendActionText' not in src:
        src, n = re.subn(
            r'(SendActionPainter\s+\*_sendAction\s*=\s*nullptr\s*;)',
            lambda m: m.group(1) + '\n\n\t[[nodiscard]] QString a11ySendActionText() const;\n',
            src,
            count=1)
        if n == 0:
            print('ERROR: TopBarWidget _sendAction landmark not found')
            sys.exit(1)
        with open(h, 'w', encoding='utf-8') as f:
            f.write(src)
        print('top_bar_widget.h patched')

    if not os.path.exists(cpp):
        return
    with open(cpp, 'r', encoding='utf-8') as f:
        src = f.read()
    if 'TopBarWidget::a11ySendActionText' not in src:
        src = src.rstrip() + (
            '\n\nQString TopBarWidget::a11ySendActionText() const {\n'
            '\treturn _sendAction ? _sendAction->actionText().simplified() : QString();\n'
            '}\n')
        with open(cpp, 'w', encoding='utf-8') as f:
            f.write(src)
        print('top_bar_widget.cpp patched')


def patch_bot_keyboard():
    h = f'{ROOT}/Telegram/SourceFiles/chat_helpers/bot_keyboard.h'
    cpp = f'{ROOT}/Telegram/SourceFiles/chat_helpers/bot_keyboard.cpp'
    if not os.path.exists(h):
        print(f'WARNING: missing {h}')
        return

    with open(h, 'r', encoding='utf-8') as f:
        hdr = f.read()
    if 'accessibilityButtonCount' not in hdr:
        if 'Q_OBJECT' not in hdr:
            hdr, n = re.subn(
                r'(,\s*public\s+ClickHandlerHost\s*\{)',
                lambda m: m.group(1) + '\n\tQ_OBJECT\n',
                hdr,
                count=1)
            if n == 0:
                print('ERROR: BotKeyboard class header not found')
                sys.exit(1)
        decl = (
            '\n\tQ_INVOKABLE int accessibilityButtonCount() const;\n'
            '\tQ_INVOKABLE QString accessibilityButtonLabel(int index) const;\n'
            '\tQ_INVOKABLE bool accessibilityActivateButton(int index);\n'
        )
        hdr, n = re.subn(
            r'(void\s+resizeToWidth\s*\([^)]*\)\s*;)',
            lambda m: m.group(1) + decl,
            hdr,
            count=1)
        if n == 0:
            print('ERROR: BotKeyboard resizeToWidth not found')
            sys.exit(1)
        with open(h, 'w', encoding='utf-8') as f:
            f.write(hdr)
        print('bot_keyboard.h patched')

    if not os.path.exists(cpp):
        return
    with open(cpp, 'r', encoding='utf-8') as f:
        body = f.read()
    if 'BotKeyboard::accessibilityButtonCount' in body:
        print('bot_keyboard.cpp already patched')
        return
    if '#include "history/history_item.h"' not in body:
        body = body.replace(
            '#include "history/history_item_components.h"\n',
            '#include "history/history_item_components.h"\n'
            '#include "history/history_item.h"\n',
            1)
    impl = '''
int BotKeyboard::accessibilityButtonCount() const {
	if (!_impl) {
		return 0;
	}
	const auto item = _controller->session().data().message(_wasForMsgId);
	if (!item) {
		return 0;
	}
	if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		int count = 0;
		for (const auto &row : markup->data.rows) {
			count += int(row.size());
		}
		return count;
	}
	return 0;
}

QString BotKeyboard::accessibilityButtonLabel(int index) const {
	if (index < 0 || !_impl) {
		return QString();
	}
	if (const auto link = _impl->getLinkByIndex(index)) {
		auto text = link->copyToClipboardText().simplified();
		if (!text.isEmpty()) {
			return text;
		}
		text = link->tooltip().simplified();
		if (!text.isEmpty()) {
			return text;
		}
	}
	const auto item = _controller->session().data().message(_wasForMsgId);
	if (!item) {
		return QString();
	}
	if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		int at = 0;
		for (const auto &row : markup->data.rows) {
			for (const auto &button : row) {
				if (at == index) {
					return button.text.simplified();
				}
				++at;
			}
		}
	}
	return QString();
}

bool BotKeyboard::accessibilityActivateButton(int index) {
	if (index < 0 || !_impl) {
		return false;
	}
	const auto link = _impl->getLinkByIndex(index);
	if (!link) {
		return false;
	}
	ActivateClickHandler(window(), link, {
		Qt::LeftButton,
		QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = base::make_weak(_controller),
		}),
	});
	return true;
}
'''
    body = body.rstrip() + impl
    with open(cpp, 'w', encoding='utf-8') as f:
        f.write(body)
    print('bot_keyboard.cpp patched')


def patch_history_announce():
    cpp = f'{ROOT}/Telegram/SourceFiles/history/history_inner_widget.cpp'
    if not os.path.exists(cpp):
        print(f'WARNING: missing {cpp}')
        return

    with open(cpp, 'r', encoding='utf-8') as f:
        src = f.read()
    if 'a11y-nvda-message-speech' in src:
        print('history_inner_widget.cpp already patched (NVDA speech)')
        return

    if 'telegram_accessibility_text.h' not in src:
        src, n = re.subn(
            r'(#include\s+"history/history_inner_widget_accessibility\.h"\s*\n)',
            lambda m: m.group(1)
                + '#include "ui/accessibility/telegram_accessibility_keyboard.h"\n'
                + '#include "ui/accessibility/telegram_accessibility_text.h"\n',
            src,
            count=1)
        if n == 0:
            print('ERROR: history_inner_widget_accessibility.h include missing')
            sys.exit(1)

    pat = re.compile(
        r'void\s+HistoryInner::announceAccessibilityFocus\s*\(\s*int\s+index\s*\)\s*\{'
        r'\s*if\s*\(\s*index\s*<\s*0\s*\)\s*\{\s*return;\s*\}\s*'
        r'accessibilityChildNameChanged\s*\(\s*index\s*\)\s*;\s*'
        r'accessibilityChildFocused\s*\(\s*index\s*\)\s*;\s*\}',
        re.DOTALL)
    repl = (
        'void HistoryInner::announceAccessibilityFocus(int index) {\n'
        '\tif (index < 0) {\n'
        '\t\treturn;\n'
        '\t}\n'
        '\taccessibilityChildNameChanged(index);\n'
        '\taccessibilityChildFocused(index);\n'
        '\t// a11y-nvda-message-speech\n'
        '\tconst auto spoken = TgAccessibility::detail::CompactAccessibilityText(\n'
        '\t\taccessibilityChildName(index));\n'
        '\tif (!spoken.isEmpty()) {\n'
        '\t\tTgAccessibility::nvda::SpeakForced(spoken);\n'
        '\t}\n'
        '}'
    )
    src2, n = pat.subn(repl, src, count=1)
    if n == 0:
        print('ERROR: announceAccessibilityFocus block not found')
        sys.exit(1)
    with open(cpp, 'w', encoding='utf-8') as f:
        f.write(src2)
    print('history_inner_widget.cpp patched (NVDA message speech)')


def main():
    patch_top_bar()
    patch_bot_keyboard()
    patch_history_announce()


if __name__ == '__main__':
    main()
