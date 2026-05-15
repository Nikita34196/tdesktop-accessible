// telegram_accessibility.cpp
// Accessibility layer for Telegram Desktop + NVDA/JAWS/Narrator
#include "ui/accessibility/telegram_accessibility.h"

#include <QApplication>
#include <QAbstractButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QListView>
#include <QScrollArea>
#include <QToolTip>
#include <QDebug>

namespace TgAccessibility {

// =====================================================================
// Install — call once from Application::init() or main()
// =====================================================================
void Install() {
    QAccessible::installFactory(&Factory);
    qDebug() << "[TgAccessibility] Screen-reader accessibility layer installed.";
}

// =====================================================================
// Factory — Qt calls this for every QObject that needs an interface
// =====================================================================
QAccessibleInterface *Factory(
    const QString &classname,
    QObject *object)
{
    if (!object || !object->isWidgetType()) {
        return nullptr;
    }

    auto *w = static_cast<QWidget *>(object);

    // ---- Buttons ----
    // Telegram uses several custom button classes.
    // Match by class name substring so we catch them all.
    if (classname.contains("Button", Qt::CaseInsensitive)
        || classname.contains("IconButton")
        || classname.contains("SendButton")
        || classname.contains("AttachButton")
        || classname.contains("EmojiButton")
        || classname.contains("TopBarWidget")  // navigation buttons
    ) {
        return new ButtonAccessible(w);
    }

    // ---- Input fields ----
    if (classname.contains("InputField")
        || classname.contains("FlatInput")
        || classname.contains("FlatTextarea")
        || classname.contains("ComposeControls")
    ) {
        return new InputFieldAccessible(w);
    }

    // ---- Lists (chats, messages, contacts) ----
    if (classname.contains("InnerWidget")      // Dialogs::InnerWidget (chat list)
        || classname.contains("ListWidget")     // HistoryView::ListWidget (messages)
        || classname.contains("PeerListContent") // contacts / members
    ) {
        return new ListAccessible(w);
    }

    // ---- Scroll areas ----
    if (classname.contains("ScrollArea")
        || classname.contains("ElasticScroll")
    ) {
        return new GenericAccessible(w, QAccessible::ScrollBar);
    }

    // ---- Panels / windows ----
    if (classname.contains("MainWidget")
        || classname.contains("MainWindow")
        || classname.contains("SectionWidget")
        || classname.contains("LayerWidget")
        || classname.contains("BoxContent")
    ) {
        return new GenericAccessible(w, QAccessible::Pane);
    }

    // ---- Labels / text ----
    if (classname.contains("FlatLabel")
        || classname.contains("LabelSimple")
    ) {
        return new GenericAccessible(w, QAccessible::StaticText);
    }

    // ---- Fallback: give every unknown Telegram widget at least a generic interface ----
    // This ensures the widget is at least "visible" to the screen reader.
    if (classname.startsWith("Ui::")
        || classname.startsWith("Dialogs::")
        || classname.startsWith("HistoryView::")
        || classname.startsWith("Window::")
        || classname.startsWith("Info::")
        || classname.startsWith("Profile::")
        || classname.startsWith("Settings::")
        || classname.startsWith("Calls::")
        || classname.startsWith("Media::")
        || classname.startsWith("ChatHelpers::")
    ) {
        return new GenericAccessible(w, QAccessible::Client);
    }

    return nullptr;
}

// =====================================================================
// GenericAccessible
// =====================================================================
GenericAccessible::GenericAccessible(QWidget *w, QAccessible::Role role)
    : QAccessibleWidget(w, role) {
}

QString GenericAccessible::text(QAccessible::Text t) const {
    auto *w = widget();
    if (!w) return {};

    switch (t) {
    case QAccessible::Name: {
        // Priority: accessibleName > toolTip > objectName
        QString name = w->accessibleName();
        if (!name.isEmpty()) return name;
        name = w->toolTip();
        if (!name.isEmpty()) {
            // Strip HTML from tooltips
            name.remove(QRegularExpression("<[^>]*>"));
            return name.trimmed();
        }
        name = w->objectName();
        if (!name.isEmpty()) {
            // Make objectName human-readable: "send_button" -> "send button"
            name.replace('_', ' ');
            return name;
        }
        // Last resort: class name
        return QString::fromUtf8(w->metaObject()->className());
    }
    case QAccessible::Description:
        return w->accessibleDescription().isEmpty()
            ? w->toolTip()
            : w->accessibleDescription();
    default:
        return QAccessibleWidget::text(t);
    }
}

QAccessible::State GenericAccessible::state() const {
    auto s = QAccessibleWidget::state();
    auto *w = widget();
    if (w) {
        s.focusable = true;
        s.focused = w->hasFocus();
        s.invisible = !w->isVisible();
        s.disabled = !w->isEnabled();
    }
    return s;
}

// =====================================================================
// ButtonAccessible
// =====================================================================
ButtonAccessible::ButtonAccessible(QWidget *w)
    : GenericAccessible(w, QAccessible::PushButton) {
}

QStringList ButtonAccessible::actionNames() const {
    return { QAccessibleActionInterface::pressAction() };
}

void ButtonAccessible::doAction(const QString &actionName) {
    if (actionName == QAccessibleActionInterface::pressAction()) {
        auto *w = widget();
        if (w) {
            // Simulate a mouse click on the widget center
            QPoint center = w->rect().center();
            QMouseEvent press(QEvent::MouseButtonPress, center,
                w->mapToGlobal(center), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, center,
                w->mapToGlobal(center), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &press);
            QApplication::sendEvent(w, &release);
        }
    }
}

// =====================================================================
// InputFieldAccessible
// =====================================================================
InputFieldAccessible::InputFieldAccessible(QWidget *w)
    : GenericAccessible(w, QAccessible::EditableText) {
}

QString InputFieldAccessible::text(QAccessible::Text t) const {
    if (t == QAccessible::Value) {
        // Try to get the text content from child QTextEdit/QPlainTextEdit
        auto *w = widget();
        if (!w) return {};

        // Telegram's InputField wraps a QTextEdit internally
        auto *te = w->findChild<QTextEdit *>();
        if (te) return te->toPlainText();

        auto *pte = w->findChild<QPlainTextEdit *>();
        if (pte) return pte->toPlainText();

        auto *le = w->findChild<QLineEdit *>();
        if (le) return le->text();

        return {};
    }

    if (t == QAccessible::Name) {
        QString name = GenericAccessible::text(t);
        if (name.isEmpty() || name == widget()->metaObject()->className()) {
            return QStringLiteral("Message input");
        }
        return name;
    }

    return GenericAccessible::text(t);
}

// =====================================================================
// ListAccessible
// =====================================================================
ListAccessible::ListAccessible(QWidget *w)
    : GenericAccessible(w, QAccessible::List) {
}

// =====================================================================
// MessageAccessible
// =====================================================================
MessageAccessible::MessageAccessible(QWidget *w)
    : GenericAccessible(w, QAccessible::StaticText) {
}

// =====================================================================
// Helper
// =====================================================================
void SetNameIfEmpty(QWidget *w, const QString &name) {
    if (w && w->accessibleName().isEmpty()) {
        w->setAccessibleName(name);
    }
}

} // namespace TgAccessibility
