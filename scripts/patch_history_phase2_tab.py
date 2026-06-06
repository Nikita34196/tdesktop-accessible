#!/usr/bin/env python3
"""Upgrade HistoryInner Tab navigation to phase 2 (all clickables, top-to-bottom)."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.environ.get('TDESKTOP_ROOT', 'tdesktop')
MARKER = 'a11y-phase2-clickables'
VOICE_SAFE_MARKER = 'a11y-phase2-voice-tab-safe-v3.1'
FORWARD_REACTION_MARKER = 'a11y-forward-reaction-structural-v1'
BLOCK_START_RE = re.compile(
    r'^\s*// === Enter/link a11y navigation \(added by accessibility-patch\) ===',
    re.MULTILINE,
)

H_PATH = f'{ROOT}/Telegram/SourceFiles/history/history_inner_widget.h'
CPP_PATH = f'{ROOT}/Telegram/SourceFiles/history/history_inner_widget.cpp'


def patch_header(src: str) -> str:
    if 'a11yCollectFocusedLinks' not in src:
        print('ERROR: patch Enter/link step before phase 2')
        sys.exit(1)

    changed = False
    if 'a11yShouldSkipTextStateTabScan' not in src:
        src, n = re.subn(
            r'(\[\[nodiscard\]\]\s+bool\s+a11yIsPlaybackDocumentMessage\s*\([^;]+;\s*)?'
            r'(std::vector<ClickHandlerPtr>\s+a11yCollectFocusedLinks\s*\(\s*\)\s*;)',
            lambda m: (m.group(1) or '')
            + m.group(2)
            + '\n\t[[nodiscard]] bool a11yShouldSkipTextStateTabScan('
            + '\n\t\tnot_null<HistoryItem*> item) const;'
            + '\n\tvoid a11yTabOnPlaybackMessage();',
            src,
            count=1,
        )
        if n == 0:
            print('ERROR: a11yCollectFocusedLinks declaration not found in .h')
            sys.exit(1)
        changed = True
    elif 'a11yTabOnPlaybackMessage' not in src:
        src, n = re.subn(
            r'(\[\[nodiscard\]\]\s+bool\s+a11yShouldSkipTextStateTabScan\s*\([^;]+;\s*)',
            lambda m: m.group(1) + '\tvoid a11yTabOnPlaybackMessage();\n',
            src,
            count=1,
        )
        if n:
            changed = True
    if 'a11yAppendStructuralClickables' not in src:
        src, n = re.subn(
            r'(std::vector<ClickHandlerPtr>\s+a11yCollectFocusedLinks\s*\(\s*\)\s*;'
            r'(?:\s*\n\t\[\[nodiscard\]\]\s+bool\s+a11yIsPlaybackDocumentMessage\s*\([^;]+;\s*)?)',
            lambda m: m.group(0)
            + '\n\tvoid a11yAppendStructuralClickables('
            + '\n\t\tnot_null<HistoryView::Element*> view,'
            + '\n\t\tnot_null<HistoryItem*> item);'
            + '\n\tvoid a11ySortFocusedLinksByPosition();',
            src,
            count=1,
        )
        if n == 0:
            print('ERROR: a11yCollectFocusedLinks declaration not found in .h')
            sys.exit(1)
        changed = True
    if '_a11yFocusedLinkSortY' not in src:
        src, n = re.subn(
            r'(std::vector<QString>\s+_a11yFocusedLinkLabels\s*;)',
            lambda m: m.group(1) + '\n\tstd::vector<int> _a11yFocusedLinkSortY;',
            src,
            count=1,
        )
        if n == 0:
            print('ERROR: _a11yFocusedLinkLabels landmark not found in .h')
            sys.exit(1)
        changed = True
    if '_a11yFocusedLinksCacheValid' not in src:
        src, n = re.subn(
            r'(std::vector<int>\s+_a11yFocusedLinkSortY\s*;)',
            lambda m: m.group(1)
            + '\n\tbool _a11yFocusedLinksCacheValid = false;'
            + '\n\tbool _a11yFocusedPlaybackOnly = false;',
            src,
            count=1,
        )
        if n == 0:
            print('ERROR: _a11yFocusedLinkSortY landmark not found in .h')
            sys.exit(1)
        changed = True
    if changed:
        print('history_inner_widget.h patched (phase 2 clickables)')
    return src


def phase2_block() -> str:
    return r'''
// === Enter/link a11y navigation (added by accessibility-patch) ===
// a11y-phase2-clickables: Tab cycles every clickable in the focused message
// (links, inline buttons, reply header, sender, media actions), top-to-bottom.
// a11y-phase2-voice-tab-safe-v3.1: Tab on voice must never call textState.
// a11y-forward-reaction-structural-v1: Tab targets for forwards and reactions.

[[nodiscard]] bool HistoryInner::a11yShouldSkipTextStateTabScan(
		not_null<HistoryItem*> item) const {
	const auto checkDocument = [](DocumentData *document) {
		if (!document) {
			return false;
		}
		return document->isVoiceMessage()
			|| document->isVideoMessage()
			|| document->isSong()
			|| document->isAudioFile();
	};
	if (const auto media = item->media()) {
		if (checkDocument(media->document())) {
			return true;
		}
	}
	if (const auto view = viewByItem(item)) {
		if (const auto media = view->media()) {
			if (checkDocument(media->getDocument())) {
				return true;
			}
		}
		for (const auto subItem : HistoryView::ActiveMessageSubItems(
				view,
				_history)) {
			if (subItem == HistoryView::MessageSubItem::Played) {
				if (const auto media = item->media()) {
					if (checkDocument(media->document())) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

void HistoryInner::a11yTabOnPlaybackMessage() {
	_a11yFocusedLinks.clear();
	_a11yFocusedLinkLabels.clear();
	_a11yFocusedLinkSortY.clear();
	_a11yFocusedPlaybackOnly = true;
	_a11yFocusedLinksCacheValid = true;
	_a11yFocusedLinkIndex = 0;
	TgAccessibility::nvda::SpeakForced(QStringLiteral(
		"Вложение 1 из 1: голосовое или аудио, Enter — воспроизведение"));
	TgAccessibility::LogLine(
		QStringLiteral("[messages] Tab on playback (guarded, no textState)"));
}

void HistoryInner::a11ySortFocusedLinksByPosition() {
	const auto n = int(_a11yFocusedLinks.size());
	if (n <= 1 || int(_a11yFocusedLinkSortY.size()) != n) {
		return;
	}
	auto order = std::vector<int>(n);
	std::iota(order.begin(), order.end(), 0);
	const auto &keys = _a11yFocusedLinkSortY;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		return keys[a] < keys[b];
	});
	const auto reorder = [&](auto &vec) {
		using Value = std::decay_t<decltype(vec[0])>;
		auto copy = std::vector<Value>(n);
		for (auto i = 0; i != n; ++i) {
			copy[i] = vec[order[i]];
		}
		vec = std::move(copy);
	};
	reorder(_a11yFocusedLinks);
	reorder(_a11yFocusedLinkLabels);
	reorder(_a11yFocusedLinkSortY);
}

void HistoryInner::a11yAppendStructuralClickables(
		not_null<HistoryView::Element*> view,
		not_null<HistoryItem*> item) {
	const auto top = itemTop(view);
	const auto sortY = [&](int localY) {
		return (top >= 0) ? (top + localY) : localY;
	};
	const auto addLink = [&](
			const ClickHandlerPtr &link,
			const QString &label,
			int y) {
		if (!link) {
			return;
		}
		if (std::find(_a11yFocusedLinks.begin(),
				_a11yFocusedLinks.end(),
				link) != _a11yFocusedLinks.end()) {
			return;
		}
		_a11yFocusedLinks.push_back(link);
		_a11yFocusedLinkLabels.push_back(label.simplified());
		_a11yFocusedLinkSortY.push_back(y);
	};
	if (const auto sender = view->fromPhotoLink()) {
		addLink(sender, QStringLiteral("Отправитель"), sortY(8));
	}
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (via->link) {
			const auto label = via->text.isEmpty()
				? QStringLiteral("Через бота")
				: via->text;
			addLink(via->link, label, sortY(20));
		}
	}
	if (const auto guest = item->Get<HistoryMessageGuestChat>()) {
		if (guest->link) {
			const auto label = guest->text.isEmpty()
				? QStringLiteral("Гость")
				: guest->text;
			addLink(guest->link, label, sortY(24));
		}
	}
	if (const auto reply = view->Get<HistoryView::Reply>()) {
		if (const auto link = reply->link()) {
			addLink(link, QStringLiteral("Ответ на сообщение"), sortY(32));
		}
	}
	if (const auto action = view->rightActionLink(std::nullopt)) {
		addLink(action, QStringLiteral("Действие справа"), sortY(40));
	}
	if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			const auto kbTop = sortY(std::max(0, view->height() - keyboard->naturalHeight() - 8));
			auto index = 0;
			for (const auto &row : markup->data.rows) {
				for (const auto &button : row) {
					const auto link = keyboard->getLinkByIndex(index);
					auto label = button.text.simplified();
					if (label.isEmpty()) {
						label = QStringLiteral("Кнопка %1").arg(index + 1);
					}
					addLink(link, label, kbTop + (index * 4));
					++index;
				}
			}
		}
	}
	// a11y-forward-reaction-structural-v1
	auto request = StateRequest();
	const auto addFromTextState = [&](
			const QPoint &localPoint,
			const QString &labelPrefix,
			int y) {
		const auto state = view->textState(localPoint, request);
		const auto link = state.link;
		if (!link) {
			return;
		}
		auto label = state.customTooltipText.simplified();
		if (label.isEmpty()) {
			label = link->tooltip().simplified();
		}
		if (label.isEmpty()) {
			label = labelPrefix.isEmpty()
				? QStringLiteral("Ссылка")
				: QStringLiteral("Пересланное сообщение");
		} else if (!labelPrefix.isEmpty()
				&& !label.startsWith(labelPrefix)) {
			label = labelPrefix + label;
		}
		addLink(link, label, sortY(y));
	};
	if (view->displayForwardedFrom()) {
		const auto h = view->height();
		const auto headerBottom = std::min(h / 3, 72);
		for (auto y = 6; y < headerBottom; y += 6) {
			for (auto x = 16; x < width(); x += 32) {
				addFromTextState(
					QPoint(x, y),
					QStringLiteral("Переслано: "),
					y);
			}
		}
		for (const auto frac : { 0.08, 0.12, 0.16 }) {
			const auto y = int(h * frac);
			addFromTextState(
				QPoint(width() / 2, y),
				QStringLiteral("Переслано: "),
				y);
		}
	}
	if (!item->reactions().empty()) {
		using namespace HistoryView::Reactions;
		const auto h = view->height();
		const auto bottomTop = std::max(h / 3, h - 96);
		for (auto y = bottomTop; y < h - 2; y += 5) {
			for (auto x = 12; x < width() - 4; x += 18) {
				const auto state = view->textState(QPoint(x, y), request);
				const auto link = state.link;
				if (!link || ReactionIdOfLink(link).empty()) {
					continue;
				}
				auto label = state.customTooltipText.simplified();
				if (label.isEmpty()) {
					label = QStringLiteral("Реакция");
				}
				const auto count = ReactionCountOfLink(item, link);
				if (count.count > 0) {
					label += QStringLiteral(" %1").arg(count.count);
				}
				addLink(link, label, sortY(y));
			}
		}
	}
	// Probing media->textState on voice/video calls Document::setSeekingStart
	// and repaint per sample — a dense Tab scan freezes or crashes Telegram.
	if (!a11yShouldSkipTextStateTabScan(item)) {
		if (const auto media = view->media()) {
			const auto h = view->height();
			const auto center = sortY(h / 2);
			for (const auto frac : { 0.35, 0.5, 0.65 }) {
				auto request = StateRequest();
				const auto state = media->textState(
					QPoint(width() / 2, int(h * frac)),
					request);
				if (state.link) {
					auto label = state.customTooltipText.simplified();
					if (label.isEmpty()) {
						label = a11yFocusedMessageSummary();
					}
					addLink(state.link, label, center);
				}
			}
		}
	}
}

std::vector<ClickHandlerPtr> HistoryInner::a11yCollectFocusedLinks() {
	if (!_accessibilityFocusedItem) {
		_a11yFocusedLinkIndex = -1;
		_a11yFocusedLinksItem = nullptr;
		_a11yFocusedLinksCacheValid = false;
		_a11yFocusedPlaybackOnly = false;
		_a11yFocusedLinks.clear();
		_a11yFocusedLinkLabels.clear();
		_a11yFocusedLinkSortY.clear();
		return _a11yFocusedLinks;
	}
	if (_a11yFocusedLinksItem != _accessibilityFocusedItem) {
		_a11yFocusedLinksItem = _accessibilityFocusedItem;
		_a11yFocusedLinkIndex = -1;
		_a11yFocusedLinksCacheValid = false;
		_a11yFocusedPlaybackOnly = false;
	}
	if (_a11yFocusedLinksCacheValid) {
		return _a11yFocusedLinks;
	}
	_a11yFocusedLinks.clear();
	_a11yFocusedLinkLabels.clear();
	_a11yFocusedLinkSortY.clear();
	_a11yFocusedPlaybackOnly = false;
	const auto view = viewByItem(_accessibilityFocusedItem);
	if (!view) {
		_a11yFocusedLinkIndex = -1;
		_a11yFocusedLinksCacheValid = true;
		return _a11yFocusedLinks;
	}
	const auto top = itemTop(view);
	if (top < 0) {
		_a11yFocusedLinkIndex = -1;
		_a11yFocusedLinksCacheValid = true;
		return _a11yFocusedLinks;
	}
	const auto item = _accessibilityFocusedItem;
	if (a11yShouldSkipTextStateTabScan(item)) {
		_a11yFocusedPlaybackOnly = true;
		_a11yFocusedLinksCacheValid = true;
		return _a11yFocusedLinks;
	}
	auto request = StateRequest();
	const auto messageSummary = a11yFocusedMessageSummary();
	const auto labelFromState = [&](const TextState &state) {
		QString detail = state.customTooltipText.simplified();
		const auto link = state.link;
		if (link) {
			if (detail.isEmpty()) detail = link->tooltip();
			if (detail.isEmpty()) detail = link->url();
			if (detail.isEmpty()) detail = link->copyToClipboardText();
			if (detail.isEmpty()) detail = link->dragText();
			if (detail.isEmpty()) detail = link->getTextEntity().data;
		}
		detail = detail.simplified();
		return detail.isEmpty() ? messageSummary : detail;
	};
	const auto add = [&](const TextState &state, int sortY) {
		const auto link = state.link;
		if (!link) return;
		if (std::find(_a11yFocusedLinks.begin(),
				_a11yFocusedLinks.end(),
				link) != _a11yFocusedLinks.end()) {
			return;
		}
		_a11yFocusedLinks.push_back(link);
		_a11yFocusedLinkLabels.push_back(labelFromState(state));
		_a11yFocusedLinkSortY.push_back(sortY);
	};
	const auto scan = [&](QPoint widgetPoint) {
		const auto itemPoint = mapPointToItem(widgetPoint, view);
		const auto state = view->textState(itemPoint, request);
		add(state, widgetPoint.y());
	};
	const auto itemHeight = view->height();
	const auto stepY = std::max(6, itemHeight / 16);
	const auto stepX = std::max(12, width() / 12);
	for (auto y = top + 4; y < top + itemHeight; y += stepY) {
		for (auto x = 4; x < width(); x += stepX) {
			scan(QPoint(x, y));
		}
	}
	for (const auto frac : { 0.5, 0.65, 0.8, 0.35, 0.95, 0.2 }) {
		scan(QPoint(width() / 2, top + int(itemHeight * frac)));
	}
	// Dense bottom band (reactions, inline keyboard, comments).
	{
		const auto bottomTop = top + (itemHeight * 2 / 3);
		for (auto y = bottomTop; y < top + itemHeight; y += 4) {
			for (auto x = 4; x < width(); x += 8) {
				scan(QPoint(x, y));
			}
		}
	}
	// Header band (forward header, reply, sender links).
	{
		const auto headerBottom = top + std::min(itemHeight / 3, 72);
		const auto headerStepY = 4;
		const auto headerStepX = std::max(8, width() / 24);
		for (auto y = top + 2; y < headerBottom; y += headerStepY) {
			for (auto x = 8; x < width(); x += headerStepX) {
				scan(QPoint(x, y));
			}
		}
		for (const auto frac : { 0.06, 0.1, 0.14, 0.18, 0.22 }) {
			scan(QPoint(width() / 2, top + int(itemHeight * frac)));
		}
	}
	a11yAppendStructuralClickables(view, item);
	a11ySortFocusedLinksByPosition();
	if (_a11yFocusedLinkIndex >= int(_a11yFocusedLinks.size())) {
		_a11yFocusedLinkIndex = -1;
	}
	_a11yFocusedLinksCacheValid = true;
	return _a11yFocusedLinks;
}

QString HistoryInner::a11yFocusedLinkLabel(int index, int total) const {
	QString detail;
	if (index >= 0 && index < int(_a11yFocusedLinkLabels.size())) {
		detail = _a11yFocusedLinkLabels[index].simplified();
	}
	if (detail.isEmpty()) {
		detail = a11yFocusedMessageSummary();
	}
	const auto link = (index >= 0 && index < int(_a11yFocusedLinks.size()))
		? _a11yFocusedLinks[index]
		: ClickHandlerPtr();
	const auto entity = link ? link->getTextEntity() : ClickHandler::TextEntity();
	const auto isMarkup = dynamic_cast<ReplyMarkupClickHandler*>(link.get());
	const auto isReaction = link
		&& !HistoryView::Reactions::ReactionIdOfLink(link).empty();
	const auto isLink = !isMarkup
		&& !isReaction
		&& link
		&& (!link->url().isEmpty() || !entity.data.isEmpty());
	const auto kind = isReaction
		? QStringLiteral("Реакция")
		: isMarkup
		? QStringLiteral("Кнопка")
		: (isLink ? QStringLiteral("Ссылка") : QStringLiteral("Вложение"));
	auto result = QStringLiteral("%1 %2 из %3")
		.arg(kind)
		.arg(index + 1)
		.arg(total);
	if (!detail.isEmpty()) {
		result += QStringLiteral(": ") + detail.left(160);
	}
	return result;
}

QString HistoryInner::a11yMessageSummaryForView(
		not_null<Element*> view) const {
	constexpr int kMaxSummaryChars = 160;
	auto parts = QStringList();
	if (const auto item = view->data()) {
		if (const auto from = item->from()) {
			const auto sender = from->name();
			if (!sender.isEmpty()) {
				parts.push_back(sender);
			}
		}
	}
	const auto active = HistoryView::ActiveMessageSubItems(view, _history);
	for (const auto subItem : active) {
		if (subItem == HistoryView::MessageSubItem::Message) {
			continue;
		}
		auto value = HistoryView::MessageSubItemValue(
			view,
			_history,
			subItem).simplified();
		if (value.isEmpty()) {
			continue;
		}
		const auto label = HistoryView::MessageSubItemLabel(subItem).simplified();
		parts.push_back(label.isEmpty()
			? value
			: (label + QStringLiteral(": ") + value));
		if (parts.size() >= 6) {
			break;
		}
	}
	if (parts.isEmpty()) {
		const auto message = HistoryView::MessageSubItemValue(
			view,
			_history,
			HistoryView::MessageSubItem::Message).simplified();
		if (!message.isEmpty()) {
			parts.push_back(message.left(120));
		}
	}
	auto result = parts.join(QStringLiteral(", "));
	if (result.isEmpty()) {
		result = view->data()->notificationText().text.simplified();
	}
	return result.left(kMaxSummaryChars);
}

QString HistoryInner::a11yFocusedMessageSummary() const {
	if (!_accessibilityFocusedItem) {
		return QString();
	}
	const auto view = viewByItem(_accessibilityFocusedItem);
	if (!view) {
		return QString();
	}
	return a11yMessageSummaryForView(view);
}

void HistoryInner::a11yMoveFocusedLink(int delta) {
	if (_accessibilityFocusedItem
			&& a11yShouldSkipTextStateTabScan(_accessibilityFocusedItem)) {
		a11yTabOnPlaybackMessage();
		return;
	}
	const auto links = a11yCollectFocusedLinks();
	if (_a11yFocusedPlaybackOnly) {
		a11yTabOnPlaybackMessage();
		return;
	}
	const auto total = int(links.size());
	if (total <= 0) {
		TgAccessibility::nvda::SpeakForced(
			QStringLiteral("В сообщении нет ссылок, кнопок или вложений"));
		TgAccessibility::LogLine(
			QStringLiteral("[messages] no clickables in focused message"));
		return;
	}
	if (_a11yFocusedLinkIndex < 0) {
		_a11yFocusedLinkIndex = (delta < 0) ? (total - 1) : 0;
	} else {
		_a11yFocusedLinkIndex = (_a11yFocusedLinkIndex + delta + total) % total;
	}
	const auto label = a11yFocusedLinkLabel(_a11yFocusedLinkIndex, total);
	TgAccessibility::nvda::SpeakForced(label);
	if (_a11yFocusedLinkIndex >= 0
			&& _a11yFocusedLinkIndex < int(_a11yFocusedLinks.size())) {
		const auto &link = _a11yFocusedLinks[_a11yFocusedLinkIndex];
		const auto entity = link
			? link->getTextEntity()
			: ClickHandler::TextEntity();
		TgAccessibility::LogLine(QStringLiteral(
			"[messages] focused clickable %1 type=%2 url=%3 data=%4")
			.arg(label.left(80))
			.arg(int(entity.type))
			.arg(link ? link->url().left(80) : QString())
			.arg(entity.data.left(80)));
	} else {
		TgAccessibility::LogLine(
			QStringLiteral("[messages] focused clickable %1").arg(label.left(80)));
	}
}

bool HistoryInner::a11yActivateFocusedLink() {
	if (_a11yFocusedPlaybackOnly) {
		TgAccessibility::LogLine(
			QStringLiteral("[messages] activate playback-only by Enter"));
		playPauseFocusedMedia();
		return true;
	}
	const auto links = a11yCollectFocusedLinks();
	if (_a11yFocusedLinkIndex < 0
			|| _a11yFocusedLinkIndex >= int(links.size())) {
		return false;
	}
	const auto link = links[_a11yFocusedLinkIndex];
	if (!link || !_accessibilityFocusedItem) {
		return false;
	}
	TgAccessibility::LogLine(QStringLiteral(
		"[messages] activate focused clickable index=%1")
		.arg(_a11yFocusedLinkIndex));
	ActivateClickHandler(
		window(),
		link,
		prepareClickContext(
			Qt::LeftButton,
			_accessibilityFocusedItem->fullId()));
	return true;
}

void HistoryInner::a11yActivateFocused() {
	if (!_accessibilityFocusedItem) {
		TgAccessibility::nvda::Speak(
			QStringLiteral("Сначала выберите сообщение стрелками"));
		return;
	}
	const auto item = _accessibilityFocusedItem;
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			if (document->isVoiceMessage()
					|| document->isSong()
					|| document->isAudioFile()
					|| document->isVideoMessage()) {
				TgAccessibility::LogLine(
					QStringLiteral("[messages] play focused media by Enter"));
				playPauseFocusedMedia();
				return;
			}
		}
	}
	const auto links = a11yCollectFocusedLinks();
	if (!links.empty()) {
		if (_a11yFocusedLinkIndex < 0) {
			_a11yFocusedLinkIndex = 0;
		}
		if (a11yActivateFocusedLink()) {
			return;
		}
	}
	const auto view = viewByItem(_accessibilityFocusedItem);
	if (!view) {
		return;
	}
	const auto top = itemTop(view);
	if (top < 0) {
		return;
	}
	const auto h = view->height();
	const auto x = width() / 2;
	auto request = StateRequest();
	ClickHandlerPtr link;
	for (const auto frac : { 0.5, 0.65, 0.8, 0.35, 0.95, 0.2 }) {
		const auto y = top + int(h * frac);
		const auto itemPoint = mapPointToItem(QPoint(x, y), view);
		const auto state = view->textState(itemPoint, request);
		if (state.link) {
			link = state.link;
			break;
		}
	}
	if (link) {
		TgAccessibility::nvda::Speak(
			QStringLiteral("Открытие файла или ссылки"));
		ActivateClickHandler(
			window(),
			link,
			prepareClickContext(
				Qt::LeftButton,
				_accessibilityFocusedItem->fullId()));
	}
}
'''


def patch_cpp(src: str) -> str:
    if FORWARD_REACTION_MARKER in src:
        print('history_inner_widget.cpp already has forward/reaction Tab targets')
        return src
    if 'a11yCollectFocusedLinks' not in src:
        print('WARNING: a11yCollectFocusedLinks missing — run Enter/link patch first')
        return src

    start_match = BLOCK_START_RE.search(src)
    if not start_match:
        print('ERROR: a11y navigation block not found in .cpp')
        sys.exit(1)

    start = start_match.start()
    # Phase-1 patch always appends this block at EOF; drop through end so we
    # never leave duplicate a11y* definitions (CI yaml-indented phase-1 body).
    new_src = src[:start].rstrip() + '\n\n' + phase2_block().strip() + '\n'
    if MARKER in src:
        print('history_inner_widget.cpp upgraded (forward/reaction Tab targets)')
    else:
        print('history_inner_widget.cpp patched (phase 2 clickables)')

    if '#include <numeric>' not in new_src:
        new_src = new_src.replace(
            '#include "history/history_inner_widget.h"\n',
            '#include "history/history_inner_widget.h"\n\n#include <numeric>\n',
            1,
        )
    if 'history/view/history_view_reply.h' not in new_src:
        new_src = new_src.replace(
            '#include "history/view/history_view_message.h"\n',
            '#include "history/view/history_view_message.h"\n'
            '#include "history/view/history_view_reply.h"\n',
            1,
        )
    if 'history/view/reactions/history_view_reactions.h' not in new_src:
        new_src = new_src.replace(
            '#include "history/view/history_view_reply.h"\n',
            '#include "history/view/history_view_reply.h"\n'
            '#include "history/view/reactions/history_view_reactions.h"\n',
            1,
        )
    return new_src


def main() -> None:
    if not os.path.exists(H_PATH):
        print(f'WARNING: missing {H_PATH}')
        return
    if not os.path.exists(CPP_PATH):
        print(f'WARNING: missing {CPP_PATH}')
        return

    with open(H_PATH, encoding='utf-8') as f:
        h = f.read()
    h2 = patch_header(h)
    if h2 != h:
        with open(H_PATH, 'w', encoding='utf-8') as f:
            f.write(h2)
        print('history_inner_widget.h patched (phase 2 clickables)')

    with open(CPP_PATH, encoding='utf-8') as f:
        cpp = f.read()
    cpp2 = patch_cpp(cpp)
    if cpp2 != cpp:
        with open(CPP_PATH, 'w', encoding='utf-8') as f:
            f.write(cpp2)
    elif FORWARD_REACTION_MARKER not in cpp:
        print('No phase 2 changes applied')


if __name__ == '__main__':
    main()
