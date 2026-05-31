// telegram_accessibility_live.h
// Periodic announcements: typing/recording status and bot-keyboard hints.
#pragma once

#include "ui/accessibility/telegram_accessibility.h"
#include "ui/accessibility/telegram_accessibility_keyboard.h"
#include "ui/accessibility/telegram_accessibility_text.h"

#include <QTimer>
#include <QPointer>
#include <QMetaObject>

namespace TgAccessibility {

namespace detail {

inline QWidget *FindBotKeyboard(QWidget *root) {
	auto *kb = FindByType(root, "BotKeyboard");
	if (kb && kb->isVisible()) {
		return kb;
	}
	return nullptr;
}

} // namespace detail

class LiveAnnouncer : public QObject {
public:
	explicit LiveAnnouncer(QObject *parent = nullptr)
		: QObject(parent) {
		_timer.setInterval(900);
		connect(&_timer, &QTimer::timeout, this, [this] { tick(); });
	}

	void start() {
		if (!_timer.isActive()) {
			_timer.start();
			LogLine(QStringLiteral("[live] announcer started"));
		}
	}

private:
	void tick() {
		QWidget *root = detail::FindMainWindow();
		if (!root) {
			return;
		}
		pollTypingStatus(root);
		pollBotKeyboard(root);
	}

	void pollTypingStatus(QWidget *root) {
		if (auto *top = detail::FindTopBar(root)) {
			const auto status = detail::InvokeStringMethod(
				top, "a11ySendActionText");
			if (status != _lastSendAction) {
				_lastSendAction = status;
				if (!status.isEmpty()) {
					nvda::Speak(QStringLiteral("Статус чата: ") + status);
					LogLine(QStringLiteral("[live] typing/status: %1")
						.arg(status.left(60)));
				}
			}
		}
	}

	void pollBotKeyboard(QWidget *root) {
		auto *kb = detail::FindBotKeyboard(root);
		const bool visible = (kb != nullptr);
		if (visible == _botKeyboardVisible) {
			return;
		}
		_botKeyboardVisible = visible;
		if (visible) {
			const int count = detail::InvokeIntMethod(
				kb, "accessibilityButtonCount");
			const auto phrase = count > 0
				? QStringLiteral(
					"Клавиатура бота, %1 кнопок. "
					"Стрелки — выбор, Enter — нажать, "
					"Ctrl+Shift+B — панель клавиатуры.")
					.arg(count)
				: QStringLiteral(
					"Клавиатура бота. Ctrl+Shift+B — к кнопкам.");
			nvda::Speak(phrase);
			LogLine(QStringLiteral("[live] bot keyboard, buttons=%1")
				.arg(count));
		} else {
			nvda::Speak(QStringLiteral("Клавиатура бота скрыта"));
		}
	}

	QTimer _timer;
	QString _lastSendAction;
	bool _botKeyboardVisible = false;
};

inline void InstallLiveAnnouncer() {
	static QPointer<LiveAnnouncer> instance;
	if (!qApp) {
		return;
	}
	if (!instance) {
		instance = new LiveAnnouncer(qApp);
	}
	instance->start();
}

} // namespace TgAccessibility
