// telegram_accessibility_text.h
// Helpers to turn rich multi-line accessibility strings into phrases
// that fit NVDA's controller-client limits.
#pragma once

#include <QString>
#include <QStringList>

namespace TgAccessibility::detail {

// One short spoken line for file/photo/video rows from MessageAccessibilityName.
inline QString ShortenAttachmentLine(const QString &line) {
	const auto s = line.simplified();
	if (s.isEmpty()) {
		return s;
	}
	const auto colon = s.indexOf(QLatin1Char(':'));
	if (colon > 0 && colon < s.size() - 1) {
		const auto kind = s.left(colon).simplified();
		const auto rest = s.mid(colon + 1).simplified();
		if (!rest.isEmpty()) {
			if (kind.contains(QStringLiteral("файл"), Qt::CaseInsensitive)
				|| kind.contains(QStringLiteral("file"), Qt::CaseInsensitive)
				|| kind.contains(QStringLiteral("документ"), Qt::CaseInsensitive)) {
				return QStringLiteral("Файл ") + rest.left(72);
			}
			if (kind.contains(QStringLiteral("фото"), Qt::CaseInsensitive)
				|| kind.contains(QStringLiteral("photo"), Qt::CaseInsensitive)) {
				return QStringLiteral("Фото") + (rest.isEmpty()
					? QString()
					: QStringLiteral(", ") + rest.left(48));
			}
			if (kind.contains(QStringLiteral("видео"), Qt::CaseInsensitive)
				|| kind.contains(QStringLiteral("video"), Qt::CaseInsensitive)) {
				return QStringLiteral("Видео") + (rest.isEmpty()
					? QString()
					: QStringLiteral(", ") + rest.left(48));
			}
		}
	}
	if (s.size() > 90) {
		return s.left(90) + QStringLiteral("…");
	}
	return s;
}


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
			"скач", "download", "МБ", "MB", "KB", "кб", "pdf", "zip",
			"rar", "exe", "doc", "xls", "ppt", "mp3", "mp4", "wav",
		})) {
			picked.push_back(ShortenAttachmentLine(line));
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
