// telegram_accessibility_text.h
// Helpers to turn rich multi-line accessibility strings into phrases
// that fit NVDA's controller-client limits.
#pragma once

#include <QString>
#include <QStringList>

namespace TgAccessibility::detail {

// Collapse MessageAccessibilityName() output (newline-separated) into one
// spoken phrase: sender, media/file line, reactions, short text preview.
inline QString CompactAccessibilityText(
		const QString &full,
		int maxChars = 110) {
	const auto lines = full.split(QChar('\n'), Qt::SkipEmptyParts);
	if (lines.isEmpty()) {
		return {};
	}

	QStringList picked;
	picked.reserve(5);

	picked.push_back(lines.front().simplified());

	auto containsAny = [](const QString &line, std::initializer_list<const char*> keys) {
		for (const auto *key : keys) {
			if (line.contains(QString::fromUtf8(key), Qt::CaseInsensitive)) {
				return true;
			}
		}
		return false;
	};

	for (int i = 1; i < lines.size(); ++i) {
		const auto line = lines[i].simplified();
		if (line.isEmpty()) {
			continue;
		}
		if (containsAny(line, {
			"файл", "file", "фото", "photo", "видео", "video",
			"аудио", "audio", "голос", "voice", "стикер", "sticker",
			"документ", "document", "GIF", "вложение", "attachment",
			"скач", "download", "МБ", "MB", "KB", "кб",
		})) {
			picked.push_back(line);
			break;
		}
	}

	for (int i = 1; i < lines.size(); ++i) {
		const auto line = lines[i].simplified();
		if (line.isEmpty()) {
			continue;
		}
		if (containsAny(line, {
			"реакц", "reaction", "👍", "❤", "🔥",
		})) {
			picked.push_back(line);
			break;
		}
	}

	for (int i = 1; i < lines.size(); ++i) {
		const auto line = lines[i].simplified();
		if (line.isEmpty() || picked.contains(line)) {
			continue;
		}
		if (containsAny(line, {
			"отправ", "sent", "получ", "received", "изменено", "edited",
			"просмотр", "view", "печата", "typing", "записывает",
		})) {
			continue;
		}
		if (line.size() >= 8) {
			picked.push_back(line.left(80));
			break;
		}
	}

	auto result = picked.join(QStringLiteral(", "));
	if (result.size() > maxChars) {
		result = result.left(maxChars) + QStringLiteral("…");
	}
	return result;
}

inline QString CompactChatListLabel(const QString &full, int maxChars = 100) {
	auto line = full.simplified();
	if (line.size() <= maxChars) {
		return line;
	}
	return line.left(maxChars) + QStringLiteral("…");
}

} // namespace TgAccessibility::detail
