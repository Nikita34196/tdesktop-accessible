// telegram_accessibility.cpp
// Accessibility layer for Telegram Desktop + NVDA/JAWS/Narrator
//
// Why this file looks the way it does:
//   * Telegram's custom widgets do NOT use Q_OBJECT, so every one of them
//     reports metaObject()->className() == "Ui::RpWidget". You cannot tell
//     them apart through Qt's meta info — we use std::type_info (typeid)
//     to read the real C++ type when matching panels.
//   * We avoid taking over Qt's built-in widget accessibility (QLineEdit,
//     QAbstractButton, etc. already have very good QAccessible factories).
//     If we returned a generic interface for those, NVDA would regress.
//   * A diagnostic log is written to <Desktop>/tg_widgets_log.txt on each
//     run, listing each new class Qt asks us about plus a one-shot widget
//     tree dump ~10 seconds after startup. That is the fastest path to
//     learning what real type names show up in a given build.
#include "ui/accessibility/telegram_accessibility.h"
#include "ui/accessibility/telegram_accessibility_keyboard.h"

// lib_ui already ships a full Ui::Accessible framework: a QAccessibleInterface
// factory keyed on Ui::RpWidget plus virtual accessibilityChild*() hooks on
// every RpWidget. Dialogs::InnerWidget overrides those hooks to expose each
// chat as a real accessible child (with proper name, role, state, and Focus
// events on Up/Down). That whole pipeline is dormant in upstream tdesktop
// because nothing ever calls Ui::Accessible::Init(). We call it below so the
// pre-existing screen-reader code light up. Our own factory becomes a
// fallback for widgets lib_ui doesn't claim (accessibilityRole() == NoRole).
#include "ui/accessible/ui_accessible_factory.h"
#include "ui/screen_reader_mode.h"

#include <QApplication>
#include <QAbstractButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QListView>
#include <QScrollArea>
#include <QToolTip>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QDateTime>
#include <typeinfo>

namespace TgAccessibility {

// =====================================================================
// Diagnostic logging — writes to <Desktop>/tg_widgets_log.txt
// QAccessible::queryAccessibleInterface can be called from various
// places, so we guard the file with a mutex. The set of seen class
// names lets us log each unique widget kind exactly once.
// =====================================================================
namespace {

QMutex g_logMutex;
QFile *g_logFile = nullptr;
QSet<QString> g_seenClasses;

void OpenLog() {
    if (g_logFile) return;
    QString dir = QStandardPaths::writableLocation(
        QStandardPaths::DesktopLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(
            QStandardPaths::HomeLocation);
    }
    if (dir.isEmpty()) return;
    g_logFile = new QFile(dir + QStringLiteral("/tg_widgets_log.txt"));
    if (!g_logFile->open(QIODevice::WriteOnly
            | QIODevice::Truncate | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
        return;
    }
    QTextStream(g_logFile)
        << "tg_widgets_log — started "
        << QDateTime::currentDateTime().toString(Qt::ISODate)
        << "\n";
    g_logFile->flush();
}

void Log(const QString &msg) {
    QMutexLocker lock(&g_logMutex);
    OpenLog();
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream(g_logFile) << msg << "\n";
        g_logFile->flush();
    }
}

// The real C++ dynamic type of *o. On MSVC typeid::name returns
// "class Foo::Bar"; we strip the "class " / "struct " prefix.
QString TypeName(const QObject *o) {
    if (!o) return {};
    QString s = QString::fromLatin1(typeid(*o).name());
    if (s.startsWith(QLatin1String("class "))) s.remove(0, 6);
    else if (s.startsWith(QLatin1String("struct "))) s.remove(0, 7);
    return s;
}

// Treat anything starting with 'Q' and lacking '::' as a Qt built-in.
// Qt already ships strong QAccessibleInterface impls for QLineEdit,
// QAbstractButton, QListView, etc. — returning non-null for them
// would clobber the built-ins.
bool IsQtBuiltin(const QString &qtClassName) {
    return qtClassName.size() > 1
        && qtClassName.startsWith(QLatin1Char('Q'))
        && !qtClassName.contains(QLatin1String("::"));
}

} // namespace

// =====================================================================
// Typeid-based rules. The classname Qt gives us is "Ui::RpWidget" for
// every Telegram custom widget, so this table is matched against the
// real typeid name instead. Update entries when the log on Desktop
// reveals new types in a build.
// =====================================================================
namespace {

struct TypeRule {
    const char *typeContains;
    const char *accessibleName; // may be nullptr
    QAccessible::Role role;
};

const TypeRule kTypeRules[] = {
    // -- Major panels --
    { "Dialogs::Widget",      "Chat list panel",  QAccessible::Pane },
    { "Dialogs::InnerWidget", "Chats",            QAccessible::List },
    { "HistoryWidget",        "Chat",             QAccessible::Pane },
    { "HistoryInner",         "Messages",         QAccessible::List },
    { "Window::MainMenu",     "Main menu",        QAccessible::Pane },
    { "MainWidget",           "Main area",        QAccessible::Pane },
    { "Window::MainWindow",   "Telegram",         QAccessible::Window },

    // -- Inputs --
    { "Ui::InputField",       nullptr,            QAccessible::EditableText },
    { "Ui::FlatInput",        nullptr,            QAccessible::EditableText },
    { "Ui::FlatTextarea",     nullptr,            QAccessible::EditableText },
    { "Ui::NumberInput",      "Number",           QAccessible::EditableText },
    { "Ui::PasswordInput",    "Password",         QAccessible::EditableText },
    { "Ui::PhoneInput",       "Phone number",     QAccessible::EditableText },

    // -- Buttons (specific names first, generic catch-all last) --
    { "Ui::IconButton",       nullptr,            QAccessible::PushButton },
    { "Ui::RoundButton",      nullptr,            QAccessible::PushButton },
    { "Ui::FlatButton",       nullptr,            QAccessible::PushButton },
    { "Ui::LinkButton",       nullptr,            QAccessible::Link },
    { "Ui::SettingsButton",   nullptr,            QAccessible::PushButton },
    { "Ui::EmojiButton",      nullptr,            QAccessible::PushButton },
    { "Ui::CrossButton",      nullptr,            QAccessible::PushButton },
    { "Ui::JumpDownButton",   nullptr,            QAccessible::PushButton },
    { "Ui::SendButton",       "Отправить",        QAccessible::PushButton },
    { "VoiceRecordButton",    "Записать голосовое сообщение", QAccessible::PushButton },
    { "ComposeAiButton",      nullptr,            QAccessible::PushButton },
    { "RecordLock",           nullptr,            QAccessible::PushButton },
    { "CancelButton",         "Отменить",         QAccessible::PushButton },
    { "Ui::AbstractButton",   nullptr,            QAccessible::PushButton },
    { "Ui::Menu::Action",     nullptr,            QAccessible::MenuItem },
    // Fallback: anything whose dynamic type name still contains "Button"
    // (e.g. third-party or namespace-qualified button subclasses we haven't
    // explicitly listed). MUST stay last among button rules so the more
    // specific entries above take precedence and keep their roles/names.
    { "Button",               nullptr,            QAccessible::PushButton },

    // -- Other --
    { "Ui::FlatLabel",        nullptr,            QAccessible::StaticText },
    { "Ui::LabelSimple",      nullptr,            QAccessible::StaticText },
    { "Ui::ScrollArea",       nullptr,            QAccessible::Pane },
    { "Ui::ElasticScroll",    nullptr,            QAccessible::Pane },
    { "Ui::LayerWidget",      "Dialog",           QAccessible::Dialog },
    { "Ui::BoxContent",       nullptr,            QAccessible::Pane },
    { "Window::SectionWidget","Section",          QAccessible::Pane },
    { "Window::TopBarWidget", "Chat header",      QAccessible::ToolBar },
};

const TypeRule *MatchByTypeName(const QString &typeName) {
    for (const auto &r : kTypeRules) {
        if (typeName.contains(
                QLatin1String(r.typeContains), Qt::CaseInsensitive)) {
            return &r;
        }
    }
    return nullptr;
}

QAccessibleInterface *MakeForRole(QWidget *w, QAccessible::Role role) {
    switch (role) {
    case QAccessible::PushButton:
    case QAccessible::Link:
        return new ButtonAccessible(w);
    case QAccessible::EditableText:
        return new InputFieldAccessible(w);
    case QAccessible::List:
        return new ListAccessible(w);
    default:
        return new GenericAccessible(w, role);
    }
}

} // namespace

// =====================================================================
// Widget tree dump — one-shot, called ~10s after Install().
// Walks every top-level widget and logs class/type/objectName/size
// so we can see what's actually on screen.
// =====================================================================
namespace {

void DumpWidget(QWidget *w, int depth) {
    if (!w) return;
    QString indent(depth * 2, QChar(' '));
    Log(QStringLiteral("%1%2  type=\"%3\"  obj=\"%4\"  acc=\"%5\"  "
                       "vis=%6  fp=%7  %8x%9")
        .arg(indent)
        .arg(QString::fromUtf8(w->metaObject()->className()))
        .arg(TypeName(w))
        .arg(w->objectName())
        .arg(w->accessibleName())
        .arg(w->isVisible() ? 1 : 0)
        .arg(int(w->focusPolicy()))
        .arg(w->width())
        .arg(w->height()));
    const auto children = w->findChildren<QWidget *>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *c : children) {
        DumpWidget(c, depth + 1);
    }
}

} // namespace

void DumpWidgetTree() {
    Log(QStringLiteral("\n=== WIDGET TREE DUMP @ %1 ===")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    for (QWidget *top : QApplication::topLevelWidgets()) {
        DumpWidget(top, 0);
    }
    Log(QStringLiteral("=== END DUMP ===\n"));
}

void LogLine(const QString &msg) {
    Log(msg);
}

// =====================================================================
// Install — call once from Application::init() or main()
// =====================================================================
void Install() {
    // Order matters. Qt's QAccessible::installFactory pushes factories onto a
    // list that is iterated in REVERSE order, so the factory installed last
    // gets the first attempt at every widget. We want lib_ui's factory tried
    // first because it knows how to expose Dialogs::InnerWidget's child rows
    // properly; ours stays as the catch-all for widgets lib_ui doesn't claim.
    QAccessible::installFactory(&Factory);
    Ui::Accessible::Init();

    Log(QStringLiteral("[TgAccessibility] Install() called"));
    Log(QStringLiteral("[Init] Ui::Accessible::Init() done; "
                       "ScreenReaderModeActive=%1, QAccessible::isActive=%2")
        .arg(Ui::ScreenReaderModeActive() ? 1 : 0)
        .arg(QAccessible::isActive() ? 1 : 0));

    // Log every screen-reader transition AND nudge NVDA to re-enumerate
    // the accessibility tree whenever it becomes active (first attach
    // OR user-triggered restart of NVDA while Telegram is running).
    //
    // Why this matters: on first launch NVDA sometimes attaches BEFORE
    // lib_ui's FocusManager has run its rpl handler that flips every
    // existing widget from focusPolicy=NoFocus to its proper
    // accessibilityFocusPolicy() value. NVDA caches the initial
    // "not focusable" view and never re-reads — so until the user
    // manually restarts NVDA, all the buttons/inputs/etc. appear dead
    // to the screen reader even though the underlying Qt accessibility
    // is now correct.
    //
    // Firing QAccessibleEvent(mainWindow, ObjectShow) is equivalent to
    // the WinEvent EVENT_OBJECT_SHOW on the main HWND, which NVDA
    // reacts to by re-enumerating the focused window's tree. That's
    // the same code path NVDA runs on its own startup, so this turns
    // a "restart NVDA to make everything readable" scenario into the
    // automatic post-init behavior.
    static rpl::lifetime kScreenReaderLifetime;
    Ui::ScreenReaderModeActiveValue(
    ) | rpl::on_next([](bool active) {
        Log(QStringLiteral("[ScreenReader] active changed -> %1")
            .arg(active ? 1 : 0));
        if (!active) return;
        // Defer one event-loop tick so lib_ui's FocusManager finishes
        // updating focus policies on existing widgets before we ask
        // NVDA to look again.
        QTimer::singleShot(0, qApp, [] {
            // Largest visible top-level window — that's MainWindow.
            QWidget *target = nullptr;
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (!w || !w->isVisible() || !w->isWindow()) continue;
                if (!target
                    || (w->width() * w->height())
                        > (target->width() * target->height())) {
                    target = w;
                }
            }
            if (!target) return;
            Log(QStringLiteral(
                "[ScreenReader] nudging NVDA: ObjectShow on %1")
                .arg(QString::fromUtf8(target->metaObject()->className())));
            QAccessibleEvent show(target, QAccessible::ObjectShow);
            QAccessible::updateAccessibility(&show);
            // ParentChanged on focus widget jolts NVDA in cases where
            // ObjectShow alone wasn't enough — it forces a re-walk of
            // the focused subtree.
            if (QWidget *focused = QApplication::focusWidget()) {
                QAccessibleEvent parentChanged(
                    focused, QAccessible::ParentChanged);
                QAccessible::updateAccessibility(&parentChanged);
            }
        });
    }, kScreenReaderLifetime);

    // Defer keyboard nav until the event loop is up and qApp / main
    // window exist.
    QTimer::singleShot(0, qApp, [] {
        InstallKeyboardNavigation();
    });

    // One-shot diagnostic dump after the UI has settled.
    QTimer::singleShot(10000, qApp, [] {
        DumpWidgetTree();
    });

    qDebug() << "[TgAccessibility] Screen-reader accessibility layer installed.";
}

// =====================================================================
// Factory — Qt calls this for every QObject that needs an interface.
// Returns nullptr to let other factories (notably Qt's built-ins)
// handle the widget; returns non-null to take over.
// =====================================================================
QAccessibleInterface *Factory(
    const QString &classname,
    QObject *object)
{
    if (!object || !object->isWidgetType()) {
        return nullptr;
    }
    auto *w = static_cast<QWidget *>(object);

    const QString typeName = TypeName(object);

    // Log every new (className, typeName) pair we see. This is what
    // lets us discover the real type strings to put in kTypeRules.
    {
        const QString key = classname + QStringLiteral("|") + typeName;
        QMutexLocker lock(&g_logMutex);
        if (!g_seenClasses.contains(key)) {
            g_seenClasses.insert(key);
            // Released before Log() to keep the lock window short.
            lock.unlock();
            Log(QStringLiteral("FACTORY  classname=\"%1\"  type=\"%2\"  "
                               "obj=\"%3\"  tip=\"%4\"")
                .arg(classname, typeName, object->objectName(),
                     w->toolTip().left(60)));
        }
    }

    // Never override Qt's own widget accessibility — those interfaces
    // are richer than anything we'd build here.
    if (IsQtBuiltin(classname)) {
        return nullptr;
    }

    // Primary path: match by real C++ type via typeid. This catches
    // Telegram-specific classes that all collapse to "Ui::RpWidget"
    // under metaObject()->className().
    if (const TypeRule *r = MatchByTypeName(typeName)) {
        if (r->accessibleName && w->accessibleName().isEmpty()) {
            w->setAccessibleName(QString::fromUtf8(r->accessibleName));
        }
        return MakeForRole(w, r->role);
    }

    // ---- Fallback: legacy classname-based matching ----
    // These rarely fire for Telegram custom widgets (className is
    // always "Ui::RpWidget"), but they catch some third-party
    // widgets whose className does carry the real name.
    if (classname.contains("Button", Qt::CaseInsensitive)) {
        return new ButtonAccessible(w);
    }
    if (classname.contains("InputField")
        || classname.contains("FlatInput")
        || classname.contains("FlatTextarea")
        || classname.contains("ComposeControls")) {
        return new InputFieldAccessible(w);
    }
    if (classname.contains("InnerWidget")
        || classname.contains("ListWidget")
        || classname.contains("PeerListContent")) {
        return new ListAccessible(w);
    }
    if (classname.contains("ScrollArea")
        || classname.contains("ElasticScroll")) {
        return new GenericAccessible(w, QAccessible::ScrollBar);
    }
    if (classname.contains("MainWidget")
        || classname.contains("MainWindow")
        || classname.contains("SectionWidget")
        || classname.contains("LayerWidget")
        || classname.contains("BoxContent")) {
        return new GenericAccessible(w, QAccessible::Pane);
    }
    if (classname.contains("FlatLabel")
        || classname.contains("LabelSimple")) {
        return new GenericAccessible(w, QAccessible::StaticText);
    }

    // Conservative final fallback: only wrap widgets that are clearly
    // in a Telegram namespace. Returning a generic Client interface
    // makes the widget at least addressable by NVDA; for anything else
    // we leave it to Qt's default behavior.
    if (classname.startsWith("Ui::")
        || classname.startsWith("Dialogs::")
        || classname.startsWith("HistoryView::")
        || classname.startsWith("Window::")
        || classname.startsWith("Info::")
        || classname.startsWith("Profile::")
        || classname.startsWith("Settings::")
        || classname.startsWith("Calls::")
        || classname.startsWith("Media::")
        || classname.startsWith("ChatHelpers::")) {
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
        // Priority: accessibleName > toolTip > windowTitle > objectName
        QString name = w->accessibleName();
        if (!name.isEmpty()) return name;
        name = w->toolTip();
        if (!name.isEmpty()) {
            name.remove(QRegularExpression("<[^>]*>"));
            return name.trimmed();
        }
        name = w->windowTitle();
        if (!name.isEmpty()) return name;
        name = w->objectName();
        if (!name.isEmpty()) {
            name.replace('_', ' ');
            return name;
        }
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
        s.focusable = (w->focusPolicy() != Qt::NoFocus);
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
    if (actionName != QAccessibleActionInterface::pressAction()) {
        return;
    }
    auto *w = widget();
    if (!w || !w->isVisible() || !w->isEnabled()) {
        return;
    }

    // Make sure keyboard focus follows the activation. Some Telegram
    // widgets check hasFocus() before reacting; setting it before the
    // mouse events also keeps NVDA's reported focus consistent.
    if (w->focusPolicy() != Qt::NoFocus) {
        w->setFocus(Qt::OtherFocusReason);
    }

    const QPoint local = w->rect().center();
    const QPoint global = w->mapToGlobal(local);

    QMouseEvent press(QEvent::MouseButtonPress, local, global,
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    // For MouseButtonRelease the `buttons` field is the state AFTER the
    // release — no buttons should remain pressed. Several widgets in
    // lib_ui check this and ignore the click if it's wrong.
    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
}

// =====================================================================
// InputFieldAccessible
// =====================================================================
InputFieldAccessible::InputFieldAccessible(QWidget *w)
    : GenericAccessible(w, QAccessible::EditableText) {
}

QString InputFieldAccessible::text(QAccessible::Text t) const {
    if (t == QAccessible::Value) {
        auto *w = widget();
        if (!w) return {};

        // Telegram's InputField wraps a QTextEdit internally
        if (auto *te = w->findChild<QTextEdit *>()) return te->toPlainText();
        if (auto *pte = w->findChild<QPlainTextEdit *>()) return pte->toPlainText();
        if (auto *le = w->findChild<QLineEdit *>()) return le->text();
        return {};
    }

    if (t == QAccessible::Name) {
        QString name = GenericAccessible::text(t);
        if (name.isEmpty()
            || name == QString::fromUtf8(widget()->metaObject()->className())) {
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
