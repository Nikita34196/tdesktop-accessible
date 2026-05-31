// telegram_accessibility_names.h
// Pre-defined accessible names for known Telegram widgets.
// Call ApplyNames(mainWindow) after UI is built.
#pragma once

#include <QWidget>
#include <QString>
#include <QLatin1String>
#include <typeinfo>

namespace TgAccessibility {

namespace detail {

inline QString DynamicTypeNameForNames(const QObject *o) {
    if (!o) return {};
    QString s = QString::fromLatin1(typeid(*o).name());
    if (s.startsWith(QLatin1String("class "))) {
        s.remove(0, 6);
    } else if (s.startsWith(QLatin1String("struct "))) {
        s.remove(0, 7);
    }
    return s;
}

} // namespace detail

// Recursively walk the widget tree and set accessible names
// based on typeid (real C++ type) and objectName heuristics.
inline void ApplyNames(QWidget *root) {
    if (!root) return;

    struct NameRule {
        const char *typeContains;    // match typeid name (preferred)
        const char *classContains;   // match metaObject()->className() (fallback)
        const char *objContains;     // match objectName()  (nullptr = any)
        const char *accessibleName;
    };

    static const NameRule rules[] = {
        // -- Buttons --
        { "SendButton",       "SendButton",       nullptr, "Отправить" },
        { "AttachButton",     "AttachButton",     nullptr, "Прикрепить файл" },
        { "EmojiButton",      "EmojiButton",      nullptr, "Эмодзи и стикеры" },
        { "BotKeyboardShow",  nullptr,            nullptr, "Показать клавиатуру бота" },
        { "VoiceRecordButton", "VoiceRecordButton", nullptr, "Записать голосовое сообщение" },
        { "RecordButton",     "RecordButton",     nullptr, "Записать голосовое сообщение" },
        { "ScheduleButton",   "ScheduleButton",   nullptr, "Отложенная отправка" },
        { "SilentToggle",     "SilentToggle",     nullptr, "Без звука" },

        // -- Top bar --
        { "TopBarWidget",     "TopBarWidget",     nullptr, "Заголовок чата" },
        { "BackButton",       "BackButton",       nullptr, "Назад" },

        // -- Input --
        { "ComposeControls",  "ComposeControls",  nullptr, "Область ввода сообщения" },
        { "InputField",       "InputField",       nullptr, "Введите сообщение" },
        { "FlatInput",        "FlatInput",        "search", "Поиск чатов" },

        // -- Chat list --
        { "Dialogs::InnerWidget", "InnerWidget", nullptr, "Список чатов" },
        { "Dialogs::Widget",      "Dialogs",       nullptr, "Панель списка чатов" },

        // -- Messages --
        { "HistoryWidget",    "HistoryWidget",    nullptr, "Чат" },
        { "HistoryInner",     "HistoryInner",     nullptr, "Сообщения" },

        // -- Panels --
        { "MainWidget",       "MainWidget",       nullptr, "Главное окно" },
        { "SideBarButton",    "SideBarButton",    nullptr, "Боковая панель" },
        { "InfoWidget",       "InfoWidget",       nullptr, "Информация о чате" },
        { "Info::Profile::InnerWidget", nullptr, nullptr, "Профиль чата" },
        { "Info::Media::ListWidget", nullptr, nullptr, "Медиа и файлы" },
        { "Info::Media::Widget", nullptr, nullptr, "Общие медиа" },
        { "HistoryView::TopBarWidget", "TopBarWidget", nullptr, "Заголовок чата" },

        // -- Calls --
        { "CallButton",       "CallButton",       nullptr, "Позвонить" },
        { "MuteButton",       "MuteButton",       nullptr, "Отключить звук" },
    };

    const int rulesCount = sizeof(rules) / sizeof(rules[0]);

    const auto children = root->findChildren<QWidget *>();
    for (auto *w : children) {
        if (!w->accessibleName().isEmpty()) continue;

        const QString type = detail::DynamicTypeNameForNames(w);
        const QString cls = QString::fromUtf8(w->metaObject()->className());
        const QString obj = w->objectName();

        for (int i = 0; i < rulesCount; ++i) {
            const bool typeMatch = type.contains(
                QLatin1String(rules[i].typeContains), Qt::CaseInsensitive);
            const bool classMatch = rules[i].classContains
                && cls.contains(
                    QLatin1String(rules[i].classContains), Qt::CaseInsensitive);
            const bool objMatch = (rules[i].objContains == nullptr)
                || obj.contains(
                    QLatin1String(rules[i].objContains), Qt::CaseInsensitive);

            if ((typeMatch || classMatch) && objMatch) {
                w->setAccessibleName(
                    QString::fromUtf8(rules[i].accessibleName));
                break;
            }
        }
    }
}

} // namespace TgAccessibility
