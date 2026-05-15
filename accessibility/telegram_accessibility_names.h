// telegram_accessibility_names.h
// Pre-defined accessible names for known Telegram widgets.
// Include this and call ApplyNames(mainWindow) after UI is built.
#pragma once

#include <QWidget>
#include <QString>
#include <QList>

namespace TgAccessibility {

// Recursively walk the widget tree and set accessible names
// based on objectName and className heuristics.
inline void ApplyNames(QWidget *root) {
    if (!root) return;

    struct NameRule {
        const char *classContains;   // match metaObject()->className()
        const char *objContains;     // match objectName()  (nullptr = any)
        const char *accessibleName;
    };

    // Table of known widgets and their human-readable names
    static const NameRule rules[] = {
        // -- Buttons --
        { "SendButton",       nullptr,           "Send message" },
        { "AttachButton",     nullptr,           "Attach file" },
        { "EmojiButton",      nullptr,           "Emoji and stickers" },
        { "BotKeyboardShow",  nullptr,           "Show bot keyboard" },
        { "RecordButton",     nullptr,           "Record voice message" },
        { "ScheduleButton",   nullptr,           "Schedule message" },
        { "SilentToggle",     nullptr,           "Silent message toggle" },

        // -- Top bar --
        { "TopBarWidget",     nullptr,           "Chat header" },
        { "BackButton",       nullptr,           "Go back" },

        // -- Input --
        { "ComposeControls",  nullptr,           "Message input area" },
        { "InputField",       nullptr,           "Type a message" },
        { "FlatInput",        "search",          "Search chats" },

        // -- Chat list --
        { "DialogsInner",     nullptr,           "Chat list" },
        { "InnerWidget",      nullptr,           "Chat list" },

        // -- Messages --
        { "HistoryWidget",    nullptr,           "Chat messages" },
        { "ListWidget",       nullptr,           "Messages" },

        // -- Panels --
        { "MainWidget",       nullptr,           "Main window" },
        { "SideBarButton",    nullptr,           "Sidebar" },
        { "InfoWidget",       nullptr,           "Chat info panel" },

        // -- Calls --
        { "CallButton",       nullptr,           "Call" },
        { "MuteButton",       nullptr,           "Mute" },
    };

    const int rulesCount = sizeof(rules) / sizeof(rules[0]);

    // Walk all descendant widgets
    const auto children = root->findChildren<QWidget *>();
    for (auto *w : children) {
        if (!w->accessibleName().isEmpty()) continue; // already named

        QString cls = QString::fromUtf8(w->metaObject()->className());
        QString obj = w->objectName();

        for (int i = 0; i < rulesCount; ++i) {
            bool classMatch = cls.contains(
                QLatin1String(rules[i].classContains), Qt::CaseInsensitive);
            bool objMatch = (rules[i].objContains == nullptr)
                || obj.contains(
                    QLatin1String(rules[i].objContains), Qt::CaseInsensitive);

            if (classMatch && objMatch) {
                w->setAccessibleName(
                    QString::fromUtf8(rules[i].accessibleName));
                break;
            }
        }
    }
}

} // namespace TgAccessibility
