// telegram_accessibility_keyboard.h
// Keyboard navigation helper for Telegram Desktop accessibility.
// Installs an event filter that adds Tab/Arrow navigation between panels.
#pragma once

#include <QObject>
#include <QWidget>
#include <QKeyEvent>
#include <QApplication>
#include <QAccessible>
#include <QDebug>
#include <QList>

namespace TgAccessibility {

class KeyboardNavigationFilter : public QObject {
    Q_OBJECT

public:
    explicit KeyboardNavigationFilter(QObject *parent = nullptr)
        : QObject(parent) {}

    // Panels to cycle through with Tab
    void setPanels(const QList<QWidget *> &panels) {
        m_panels = panels;
    }

    void addPanel(QWidget *w) {
        if (w && !m_panels.contains(w)) {
            m_panels.append(w);
        }
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() != QEvent::KeyPress) {
            return QObject::eventFilter(obj, event);
        }

        auto *ke = static_cast<QKeyEvent *>(event);

        // F6: cycle between major panels (common screen-reader convention)
        if (ke->key() == Qt::Key_F6) {
            cyclePanels(ke->modifiers() & Qt::ShiftModifier);
            return true;
        }

        // Ctrl+Tab: also cycle panels
        if (ke->key() == Qt::Key_Tab
            && (ke->modifiers() & Qt::ControlModifier)) {
            cyclePanels(ke->modifiers() & Qt::ShiftModifier);
            return true;
        }

        // Escape: move focus to chat list (common expectation)
        if (ke->key() == Qt::Key_Escape) {
            for (auto *w : m_panels) {
                QString cls = QString::fromUtf8(
                    w->metaObject()->className());
                if (cls.contains("Dialogs") || cls.contains("InnerWidget")) {
                    w->setFocus(Qt::ShortcutFocusReason);
                    announceWidget(w);
                    return true;
                }
            }
        }

        return QObject::eventFilter(obj, event);
    }

private:
    QList<QWidget *> m_panels;

    void cyclePanels(bool reverse) {
        if (m_panels.isEmpty()) return;

        // Remove destroyed widgets
        m_panels.removeAll(nullptr);

        // Find currently focused panel
        QWidget *focused = QApplication::focusWidget();
        int currentIndex = -1;

        for (int i = 0; i < m_panels.size(); ++i) {
            if (m_panels[i] == focused
                || m_panels[i]->isAncestorOf(focused)) {
                currentIndex = i;
                break;
            }
        }

        // Move to next/previous
        int next;
        if (reverse) {
            next = (currentIndex <= 0)
                ? m_panels.size() - 1
                : currentIndex - 1;
        } else {
            next = (currentIndex + 1) % m_panels.size();
        }

        auto *target = m_panels[next];
        if (target && target->isVisible()) {
            target->setFocus(Qt::TabFocusReason);
            announceWidget(target);
        }
    }

    void announceWidget(QWidget *w) {
        if (!w) return;
        // Notify the screen reader about the focus change
        auto *iface = QAccessible::queryAccessibleInterface(w);
        if (iface) {
            QAccessibleEvent ev(w, QAccessible::Focus);
            QAccessible::updateAccessibility(&ev);
        }
    }
};

} // namespace TgAccessibility
