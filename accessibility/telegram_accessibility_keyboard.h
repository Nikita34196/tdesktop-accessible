// telegram_accessibility_keyboard.h
// Keyboard navigation helper for Telegram Desktop accessibility.
// Installs a global event filter that implements F6 / Shift+F6 / Ctrl+Tab
// navigation between the major UI panels (Chats / Messages / Message input).
//
// IMPORTANT design note:
// Telegram custom widgets do NOT use the Q_OBJECT macro (only the lib_ui
// base class Ui::RpWidget does). So QObject::metaObject()->className()
// returns "Ui::RpWidget" for every panel — you cannot tell them apart by
// the Qt meta-info. We use std::type_info (typeid) instead, which on
// MSVC returns strings like "class Dialogs::Widget".
#pragma once

#include <QObject>
#include <QWidget>
#include <QKeyEvent>
#include <QApplication>
#include <QAccessible>
#include <QDebug>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QLatin1String>
#include <typeinfo>

namespace TgAccessibility {
namespace detail {

// Returns the demangled, namespace-qualified C++ type name of *o.
// MSVC: typeid(*o).name() already returns "class Foo::Bar" — we strip
// the leading "class " / "struct " keyword so the result is "Foo::Bar".
// On GCC/Clang the name is mangled; we fall back to comparing the raw
// string, which still works as long as both sides use the same compiler.
inline QString DynamicTypeName(const QObject *o) {
    if (!o) return {};
    const QByteArray raw(typeid(*o).name());
    QString s = QString::fromLatin1(raw);
    if (s.startsWith(QLatin1String("class "))) {
        s.remove(0, 6);
    } else if (s.startsWith(QLatin1String("struct "))) {
        s.remove(0, 7);
    }
    return s;
}

// Find the first visible descendant of `root` whose dynamic type name
// equals `fullName`. Checks the root itself first.
inline QWidget *FindByType(QWidget *root, const char *fullName) {
    if (!root) return nullptr;
    const QString target = QString::fromLatin1(fullName);
    if (DynamicTypeName(root) == target && root->isVisible()) {
        return root;
    }
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (!w || !w->isVisible()) continue;
        if (DynamicTypeName(w) == target) return w;
    }
    return nullptr;
}

inline QWidget *FindMainWindow() {
    QWidget *best = nullptr;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (!w || !w->isVisible() || !w->isWindow()) continue;
        // Telegram's main window inherits Ui::RpWindow which inherits
        // Ui::RpWidget. There's only one large top-level window in the
        // common case — pick the largest visible one.
        if (!best
            || (w->width() * w->height())
                > (best->width() * best->height())) {
            best = w;
        }
    }
    return best;
}

} // namespace detail

class KeyboardNavigationFilter : public QObject {
    Q_OBJECT

public:
    explicit KeyboardNavigationFilter(QObject *parent = nullptr)
        : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() != QEvent::KeyPress) {
            return QObject::eventFilter(obj, event);
        }

        auto *ke = static_cast<QKeyEvent *>(event);
        const int key = ke->key();
        const auto mods = ke->modifiers();

        if (key == Qt::Key_F6) {
            cyclePanels(mods & Qt::ShiftModifier);
            return true;
        }
        if (key == Qt::Key_Tab && (mods & Qt::ControlModifier)) {
            cyclePanels(mods & Qt::ShiftModifier);
            return true;
        }
        if (key == Qt::Key_Escape) {
            const auto panels = discoverPanels();
            for (const auto &p : panels) {
                if (p.second == QLatin1String("Chats")) {
                    focusAndAnnounce(p.first, p.second);
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    using Panel = QPair<QWidget *, QString>;

    void cyclePanels(bool reverse) {
        const auto panels = discoverPanels();
        if (panels.isEmpty()) {
            qDebug() << "[TgAccessibility] F6: no panels discovered yet";
            return;
        }

        QWidget *focused = QApplication::focusWidget();
        int currentIndex = -1;
        for (int i = 0; i < panels.size(); ++i) {
            QWidget *p = panels[i].first;
            if (!p) continue;
            if (p == focused || (focused && p->isAncestorOf(focused))) {
                currentIndex = i;
                break;
            }
        }

        int next;
        if (reverse) {
            next = (currentIndex <= 0)
                ? panels.size() - 1
                : currentIndex - 1;
        } else {
            next = (currentIndex + 1) % panels.size();
        }

        focusAndAnnounce(panels[next].first, panels[next].second);
    }

    // Build the F6 cycle. Each entry: (focusable widget, spoken name).
    // We look up panels by their concrete C++ type via typeid, because
    // none of Telegram's widgets have Q_OBJECT and metaObject()
    // collapses them all to "Ui::RpWidget".
    QList<Panel> discoverPanels() {
        QList<Panel> out;
        QWidget *root = detail::FindMainWindow();
        if (!root) return out;

        // 1) Chat list panel — focus the inner list so arrow keys work.
        if (auto *outer = detail::FindByType(root, "Dialogs::Widget")) {
            QWidget *focusTarget = detail::FindByType(
                outer, "Dialogs::InnerWidget");
            out.append({ focusTarget ? focusTarget : outer, "Chats" });
        }

        // 2) Message history — focus the inner scrollable list.
        QWidget *history = detail::FindByType(root, "HistoryWidget");
        if (history) {
            QWidget *focusTarget = detail::FindByType(
                history, "HistoryInner");
            out.append({ focusTarget ? focusTarget : history, "Messages" });
        }

        // 3) Message input — Ui::InputField INSIDE the HistoryWidget
        // (there are other InputFields elsewhere, e.g. dialog search).
        if (history) {
            if (QWidget *input = detail::FindByType(
                    history, "Ui::InputField")) {
                out.append({ input, "Message input" });
            }
        }

        return out;
    }

    void focusAndAnnounce(QWidget *w, const QString &name) {
        if (!w) return;
        if (!name.isEmpty()) {
            // Set every time: screen readers cache the name at focus.
            w->setAccessibleName(name);
        }
        if (w->focusPolicy() == Qt::NoFocus) {
            // Most Telegram panels don't accept focus by default.
            w->setFocusPolicy(Qt::StrongFocus);
        }
        w->setFocus(Qt::ShortcutFocusReason);

        QAccessibleEvent ev(w, QAccessible::Focus);
        QAccessible::updateAccessibility(&ev);
        qDebug() << "[TgAccessibility] F6 ->" << name
                 << "type=" << detail::DynamicTypeName(w);
    }
};

inline KeyboardNavigationFilter *Filter() {
    static QPointer<KeyboardNavigationFilter> instance;
    if (!instance && qApp) {
        instance = new KeyboardNavigationFilter(qApp);
    }
    return instance.data();
}

inline void InstallKeyboardNavigation() {
    static bool installed = false;
    if (installed) return;
    if (!qApp) {
        qWarning() << "[TgAccessibility] qApp not ready; "
                      "skipping keyboard install";
        return;
    }
    auto *f = Filter();
    if (!f) return;
    qApp->installEventFilter(f);
    installed = true;
    qDebug() << "[TgAccessibility] F6 keyboard navigation installed";
}

} // namespace TgAccessibility
