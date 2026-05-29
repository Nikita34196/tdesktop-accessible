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
#include "ui/accessibility/telegram_accessibility_names.h"
#include "ui/accessibility/telegram_accessibility_live.h"

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
#include <QFileInfo>
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

    const QString path = dir + QStringLiteral("/tg_widgets_log.txt");

    // Cap log size at 5 MiB. If it grew past that across sessions,
    // rotate by moving the current file aside so a fresh one starts.
    // Keeps the previous session's log around for one more launch
    // without unbounded growth.
    constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;
    if (QFileInfo(path).size() > kMaxLogBytes) {
        const QString oldPath = path + QStringLiteral(".old");
        QFile::remove(oldPath);
        QFile::rename(path, oldPath);
    }

    g_logFile = new QFile(path);
    // Append mode: previous session's lines stay, so a user testing
    // arrow-key navigation and then sending the file gets the full
    // history including any [nvda] rc lines from before they relaunched
    // Telegram. Truncate-on-open ate those before — we used to lose
    // every speakText return code the moment the app restarted.
    if (!g_logFile->open(QIODevice::WriteOnly
            | QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
        return;
    }
    QTextStream(g_logFile)
        << "\n=== tg_widgets_log — session started "
        << QDateTime::currentDateTime().toString(Qt::ISODate)
        << " ===\n";
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

// Some technical base class names are useless/noisy for screen readers.
// Returning them causes announcements like "Ui RpWidget" in NVDA/Narrator.
bool IsTechnicalAccessibleName(QStringView name) {
    return name == QLatin1String("Ui::RpWidget")
        || name == QLatin1String("Ui:RpWidget")
        || name == QLatin1String("RpWidget")
        || name == QLatin1String("QWidget");
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
    { "Dialogs::Widget",      "Панель списка чатов", QAccessible::Pane },
    { "Dialogs::InnerWidget", "Список чатов",        QAccessible::List },
    { "HistoryWidget",        "Чат",                 QAccessible::Pane },
    { "HistoryInner",         "Сообщения",           QAccessible::List },
    { "Window::MainMenu",     "Главное меню",        QAccessible::Pane },
    { "MainWidget",           "Основная область",    QAccessible::Pane },
    { "Window::MainWindow",   "Telegram",            QAccessible::Window },

    // -- Inputs --
    { "Ui::InputField",       "Введите сообщение",   QAccessible::EditableText },
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
    { "AttachButton",         "Прикрепить файл",  QAccessible::PushButton },
    { "Ui::EmojiButton",      "Эмодзи и стикеры", QAccessible::PushButton },
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
    { "Window::TopBarWidget", "Заголовок чата",   QAccessible::ToolBar },
    { "ComposeControls",      "Область ввода сообщения", QAccessible::Pane },
    { "BotKeyboard",          "Клавиатура бота",         QAccessible::List },
    { "HistoryView::TopBarWidget", "Заголовок чата",   QAccessible::ToolBar },
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

    // Log every screen-reader transition. When NVDA becomes active,
    // fire a one-shot speech self-test through the controller client.
    //
    // The ObjectShow "nudge" that used to live here (fire
    // QAccessibleEvent(mainWindow, ObjectShow) to make NVDA
    // re-enumerate) was removed: it correlated exactly with the
    // regression where even the first chat stopped being announced.
    // Forcing NVDA to re-walk the tree from the window root appears
    // to drop its tracking of the focused list child.
    //
    // The self-test is purely diagnostic. The latest log shows every
    // nvdaController_speakText returning rc=0 (success) while the user
    // hears nothing — so we need to know whether the controller path
    // produces ANY audio at all. SelfTest() speaks one fixed phrase a
    // few seconds after NVDA attaches:
    //   * heard      -> controller speech works; the arrow-key silence
    //                   is a navigation/timing issue, keep digging there
    //   * not heard  -> NVDA accepts the RPC but never voices it;
    //                   the problem is NVDA-side and we change approach
    static rpl::lifetime kScreenReaderLifetime;
    Ui::ScreenReaderModeActiveValue(
    ) | rpl::on_next([](bool active) {
        Log(QStringLiteral("[ScreenReader] active changed -> %1")
            .arg(active ? 1 : 0));
        if (!active) return;
        // Delay so NVDA finishes its own attach/startup chatter
        // before our test phrase, otherwise NVDA might interrupt it.
        QTimer::singleShot(2500, qApp, [] {
            TgAccessibility::nvda::SelfTest();
        });

        // Targeted re-enumeration nudge.
        //
        // After an app restart NVDA can attach and walk the accessibility
        // tree before lib_ui's FocusManager has finished wiring the
        // dialogs list (ScreenReaderModeActive only just became true).
        // It then caches an empty/stale child list for Dialogs::InnerWidget
        // and never re-reads it — the chat list goes silent under arrows
        // even though Focus/Selection events fire with correct child
        // names (confirmed in tg_a11y_diag.txt / tg_widgets_log.txt).
        //
        // We can't fix this with an ObjectShow on the main window: that
        // re-walk from the window root drops NVDA's focus tracking (the
        // regression that broke even the first chat). Instead, once the
        // list exists, fire a layered burst on the InnerWidget ITSELF so
        // NVDA re-reads only that subtree, leaving the window root and
        // the focus chain untouched.
        //
        // We retry at 1.2s / 3s / 6s because:
        //   * dialog rows are populated asynchronously after sign-in
        //     restore — first attempt may run while childCount is small
        //   * NVDA does not always pick up the first burst; spacing the
        //     bursts increases the chance one lands while NVDA is ready
        //     to refresh its cache
        const auto nudge = [] {
            QWidget *root = TgAccessibility::detail::FindMainWindow();
            if (!root) return;
            QWidget *list = TgAccessibility::detail::FindByType(
                root, "Dialogs::InnerWidget");
            if (!list) return;
            QAccessibleEvent show(list, QAccessible::ObjectShow);
            QAccessible::updateAccessibility(&show);
            QAccessibleEvent reorder(list, QAccessible::ObjectReorder);
            QAccessible::updateAccessibility(&reorder);
            QAccessibleEvent renamed(list, QAccessible::NameChanged);
            QAccessible::updateAccessibility(&renamed);
            auto *iface = QAccessible::queryAccessibleInterface(list);
            const int count = iface ? iface->childCount() : -1;
            Log(QStringLiteral("[ScreenReader] burst (Show/Reorder/Name) "
                               "on Dialogs::InnerWidget childCount=%1")
                .arg(count));
        };
        QTimer::singleShot(1200, qApp, nudge);
        QTimer::singleShot(3000, qApp, nudge);
        QTimer::singleShot(6000, qApp, nudge);
    }, kScreenReaderLifetime);

    // Defer keyboard nav until the event loop is up and qApp / main
    // window exist.
    QTimer::singleShot(0, qApp, [] {
        InstallKeyboardNavigation();
        InstallLiveAnnouncer();
    });

    // One-shot diagnostic dump after the UI has settled.
    QTimer::singleShot(10000, qApp, [] {
        DumpWidgetTree();
    });

    // Apply human-readable names once the main window and compose
    // controls exist. Retries catch late-created widgets after sign-in.
    const auto applyNames = [] {
        if (QWidget *root = TgAccessibility::detail::FindMainWindow()) {
            ApplyNames(root);
            Log(QStringLiteral("[TgAccessibility] ApplyNames() on main window"));
        }
    };
    QTimer::singleShot(3000, qApp, applyNames);
    QTimer::singleShot(8000, qApp, applyNames);

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
        // Priority: accessibleName > toolTip > windowTitle > objectName.
        // Do not fall back to metaObject()->className() for Telegram
        // widgets: they all report "Ui::RpWidget" and NVDA would read that.
        QString name = w->accessibleName();
        if (!name.isEmpty()) {
            return IsTechnicalAccessibleName(name) ? QString() : name;
        }
        name = w->toolTip();
        if (!name.isEmpty()) {
            name.remove(QRegularExpression("<[^>]*>"));
            name = name.trimmed();
            if (!name.isEmpty() && !IsTechnicalAccessibleName(name)) {
                return name;
            }
        }
        name = w->windowTitle();
        if (!name.isEmpty() && !IsTechnicalAccessibleName(name)) {
            return name;
        }
        name = w->objectName();
        if (!name.isEmpty()) {
            name.replace('_', ' ');
            name = name.trimmed();
            if (!name.isEmpty() && !IsTechnicalAccessibleName(name)) {
                return name;
            }
        }
        return {};
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
            return QStringLiteral("Поле ввода");
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
