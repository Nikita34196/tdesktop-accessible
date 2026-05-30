// telegram_simple_accessibility.h
// Accessibility layer for Telegram Desktop + NVDA/JAWS/Narrator
// Drop this into Telegram/SourceFiles/ui/accessibility/
#pragma once

#include <QAccessible>
#include <QAccessibleWidget>
#include <QAccessibleObject>
#include <QWidget>
#include <QString>

namespace TgSimpleA11y {

// Call once at application startup
void Install();

// Start periodic status announcements (typing, bot keyboard).
void InstallLiveAnnouncer();

// Factory: Qt calls this to get an accessible interface for any QObject
QAccessibleInterface *Factory(const QString &classname, QObject *object);

// Diagnostic: dump the full widget tree to <Desktop>/tg_simple_log.txt.
// Install() schedules this 10 seconds after startup; expose it so other
// code (e.g. a debug hotkey) can request a dump on demand.
void DumpWidgetTree();

// Diagnostic: append a free-form line to the same log file. Safe to call
// from any thread.
void LogLine(const QString &msg);

// ------------------------------------------------------------------
// Generic accessible wrapper for any Telegram custom widget.
// Reads accessibleName / accessibleDescription / toolTip / objectName
// and exposes them to the screen reader with a configurable role.
// ------------------------------------------------------------------
class GenericAccessible : public QAccessibleWidget {
public:
    GenericAccessible(QWidget *w, QAccessible::Role role = QAccessible::Client);

    QString text(QAccessible::Text t) const override;
    QAccessible::State state() const override;
};

// ------------------------------------------------------------------
// Button-like widgets (send, attach, emoji, etc.)
// ------------------------------------------------------------------
class ButtonAccessible : public GenericAccessible {
public:
    explicit ButtonAccessible(QWidget *w);
    QStringList actionNames() const override;
    void doAction(const QString &actionName) override;
};

// ------------------------------------------------------------------
// Text input fields
// ------------------------------------------------------------------
class InputFieldAccessible : public GenericAccessible {
public:
    explicit InputFieldAccessible(QWidget *w);
    QString text(QAccessible::Text t) const override;
};

// ------------------------------------------------------------------
// Scrollable list (chat list, message list, contact list)
// ------------------------------------------------------------------
class ListAccessible : public GenericAccessible {
public:
    explicit ListAccessible(QWidget *w);
};

// ------------------------------------------------------------------
// Individual message bubble
// ------------------------------------------------------------------
class MessageAccessible : public GenericAccessible {
public:
    explicit MessageAccessible(QWidget *w);
};

// ------------------------------------------------------------------
// Helper: set accessible name on a widget if it has none
// ------------------------------------------------------------------
void SetNameIfEmpty(QWidget *w, const QString &name);

} // namespace TgSimpleA11y
