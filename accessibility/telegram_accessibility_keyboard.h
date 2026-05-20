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
#include <QTimer>
#include <QLatin1String>
#include <typeinfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>
#include <system_error>
#endif

#include "ui/accessibility/telegram_accessibility.h"

namespace TgAccessibility {

// =====================================================================
// NVDA Controller Client integration.
//
// Qt's MSAA bridge fires Focus / Selection / StateChange events on the
// list parent with a child id when arrow keys move selection — and per
// our tg_a11y_diag.txt those events DO fire (focusChild returns the
// right ListItem with the chat name baked in). But NVDA still doesn't
// speak the new row, presumably because the underlying Win32 focus
// stays on the parent HWND and NVDA filters that as "no real focus
// change".
//
// nvdaControllerClient.dll is NVDA's documented IPC entrypoint: any
// process can LoadLibrary it and call nvdaController_speakText to make
// NVDA speak arbitrary text immediately, bypassing the entire MSAA
// path. Distributed inside the NVDA install; we just search the common
// install locations. If the DLL isn't found (NVDA not installed, or
// at an unusual path) this falls back to silence — the user still has
// the on-screen highlight to follow visually.
// =====================================================================
namespace nvda {

#ifdef Q_OS_WIN

using SpeakTextFn = long(__stdcall *)(const wchar_t *);
using CancelSpeechFn = long(__stdcall *)();

inline SpeakTextFn &SpeakPtr() { static SpeakTextFn p = nullptr; return p; }
inline CancelSpeechFn &CancelPtr() {
    static CancelSpeechFn p = nullptr;
    return p;
}

// Walk the running process list and return the directory of nvda.exe
// (or an empty string if it isn't running). This is the most reliable
// way to find the NVDA install — works for default installs, portable
// installs, custom paths, anything where the user has actually launched
// NVDA. The companion DLL ships in the same directory.
inline std::wstring FindRunningNvdaDir() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    std::wstring result;

    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"nvda.exe") != 0) {
                continue;
            }
            HANDLE proc = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                entry.th32ProcessID);
            if (!proc) {
                continue;
            }
            wchar_t buf[MAX_PATH] = {};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(proc, 0, buf, &len)) {
                std::wstring path(buf, len);
                const auto slash = path.find_last_of(L"\\/");
                if (slash != std::wstring::npos) {
                    result = path.substr(0, slash);
                }
            }
            CloseHandle(proc);
            if (!result.empty()) {
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return result;
}

inline void EnsureLoaded() {
    static bool tried = false;
    if (tried) return;
    tried = true;

    // NVDA renamed the controller client DLL between releases:
    //   * Modern (2024+):  nvdaControllerClient.dll
    //                      (arch encoded in the source ZIP's folder)
    //   * Legacy:          nvdaControllerClient64.dll / *32.dll
    // Try both at every search path, modern first.
    static const wchar_t *const kDllNames[] = {
        L"nvdaControllerClient.dll",
        L"nvdaControllerClient64.dll",
    };

    auto tryLoadAt = [](const QString &label,
                        const std::wstring &dirPath) -> HMODULE {
        for (auto name : kDllNames) {
            const std::wstring fullPath = dirPath.empty()
                ? std::wstring(name)
                : (dirPath + L"\\" + name);
            HMODULE m = LoadLibraryW(fullPath.c_str());
            const QString shown = dirPath.empty()
                ? QString::fromWCharArray(name)
                : QString::fromStdWString(fullPath);
            LogLine(QStringLiteral("[nvda] try %1 -> %2")
                .arg(label.isEmpty()
                        ? shown
                        : QStringLiteral("%1 (%2)").arg(label, shown))
                .arg(m
                        ? QStringLiteral("loaded")
                        : QStringLiteral("not found")));
            if (m) return m;
        }
        return nullptr;
    };

    // First try the default search order (process dir, then PATH).
    // After this lands, our installer/portable ZIP drops
    // nvdaControllerClient.dll right next to Telegram.exe, so this
    // path normally wins and the rest of EnsureLoaded never runs.
    HMODULE dll = tryLoadAt(QStringLiteral("default search path"), L"");

    // Walk a directory tree looking for either DLL name. NVDA's own
    // install lays them out under arch-specific subdirs (x64/, x86/,
    // arm64/) and the layout has changed across releases — depth-first
    // beats hardcoding sub-paths.
    auto findInTree = [](const std::wstring &root) -> std::wstring {
        if (root.empty()) return {};
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || ec) return {};
        const auto opts =
            std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(
                root, opts, ec);
            !ec && it != std::filesystem::recursive_directory_iterator{};
            it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            std::error_code statEc;
            if (!it->is_regular_file(statEc) || statEc) continue;
            const auto name = it->path().filename().wstring();
            for (auto wanted : kDllNames) {
                if (_wcsicmp(name.c_str(), wanted) == 0) {
                    return it->path().wstring();
                }
            }
        }
        return {};
    };

    if (!dll) {
        if (auto running = FindRunningNvdaDir(); !running.empty()) {
            // Try the install root first, then walk the whole tree.
            dll = tryLoadAt(
                QStringLiteral("running nvda.exe dir"),
                running);
            if (!dll) {
                if (auto found = findInTree(running); !found.empty()) {
                    HMODULE m = LoadLibraryW(found.c_str());
                    LogLine(QStringLiteral(
                        "[nvda] recursive in %1 found %2 -> %3")
                        .arg(QString::fromStdWString(running),
                             QString::fromStdWString(found),
                             m ? QStringLiteral("loaded")
                               : QStringLiteral("LoadLibrary failed")));
                    dll = m;
                } else {
                    LogLine(QStringLiteral(
                        "[nvda] recursive scan of %1 didn't find any "
                        "nvdaControllerClient*.dll")
                        .arg(QString::fromStdWString(running)));
                }
            }
        } else {
            LogLine(QStringLiteral(
                "[nvda] no nvda.exe found in running processes"));
        }
    }

    auto envDir = [](const wchar_t *var) -> std::wstring {
        wchar_t buf[MAX_PATH] = {};
        if (GetEnvironmentVariableW(var, buf, MAX_PATH)) {
            return std::wstring(buf);
        }
        return {};
    };

    auto tryEnvDir = [&](const QString &label,
                         const wchar_t *envVar,
                         const wchar_t *subPath) -> HMODULE {
        if (dll) return dll;
        const auto base = envDir(envVar);
        if (base.empty()) return nullptr;
        return (dll = tryLoadAt(label, base + subPath));
    };

    tryEnvDir(QStringLiteral("ProgramW6432\\NVDA"),
              L"ProgramW6432", L"\\NVDA");
    tryEnvDir(QStringLiteral("ProgramFiles\\NVDA"),
              L"ProgramFiles", L"\\NVDA");
    tryEnvDir(QStringLiteral("ProgramFiles(x86)\\NVDA"),
              L"ProgramFiles(x86)", L"\\NVDA");
    tryEnvDir(QStringLiteral("LOCALAPPDATA\\Programs\\NVDA"),
              L"LOCALAPPDATA", L"\\Programs\\NVDA");
    tryEnvDir(QStringLiteral("APPDATA\\nvda"),
              L"APPDATA", L"\\nvda");

    if (dll) {
        SpeakPtr() = reinterpret_cast<SpeakTextFn>(
            GetProcAddress(dll, "nvdaController_speakText"));
        CancelPtr() = reinterpret_cast<CancelSpeechFn>(
            GetProcAddress(dll, "nvdaController_cancelSpeech"));
        LogLine(QStringLiteral(
            "[nvda] controller client loaded; speak=%1 cancel=%2")
            .arg(SpeakPtr() ? 1 : 0)
            .arg(CancelPtr() ? 1 : 0));
    } else {
        LogLine(QStringLiteral(
            "[nvda] controller client DLL not found anywhere; "
            "speech-via-controller disabled"));
    }
}

inline void Speak(const QString &text) {
    EnsureLoaded();
    if (text.isEmpty()) return;
    // Cancel any in-flight speech first so rapid Up/Down arrows don't
    // pile up a long queue we'd have to wait through.
    if (auto cancel = CancelPtr()) {
        cancel();
    }
    if (auto speak = SpeakPtr()) {
        speak(reinterpret_cast<const wchar_t *>(text.utf16()));
    }
}

#else // Q_OS_WIN

inline void Speak(const QString &) {}

#endif // Q_OS_WIN

} // namespace nvda

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

// Same as FindByType but doesn't filter out invisible widgets. Some
// widgets (like VoiceRecordBar) only become visible once we activate
// them — we need a pointer before that happens.
inline QWidget *FindByTypeAny(QWidget *root, const char *fullName) {
    if (!root) return nullptr;
    const QString target = QString::fromLatin1(fullName);
    if (DynamicTypeName(root) == target) {
        return root;
    }
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (!w) continue;
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

        // Ctrl+Shift+F6 — diagnostic: dump the widget tree now,
        // so we don't have to wait for the 10s scheduled dump.
        // Must be checked BEFORE the plain F6 case.
        if (key == Qt::Key_F6
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            DumpWidgetTree();
            return true;
        }
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
        // Diagnostic: log arrow keys when focus is on a list-like panel.
        // Logs the focused widget's type, hasFocus state, and what its
        // focusChild() interface returns (name/role). If focusChild
        // returns the right child but NVDA is still silent, the breakage
        // is in the Qt MSAA bridge or NVDA's filter. If it returns null
        // or wrong, the breakage is upstream of the bridge.
        if ((key == Qt::Key_Up
                || key == Qt::Key_Down
                || key == Qt::Key_PageUp
                || key == Qt::Key_PageDown)
            && !(mods & ~Qt::ShiftModifier)) {
            QWidget *focused = QApplication::focusWidget();
            if (focused) {
                const auto type = detail::DynamicTypeName(focused);
                if (type == QLatin1String("Dialogs::InnerWidget")
                    || type == QLatin1String("HistoryInner")) {
                    // Let the keypress run normally first, then read state.
                    QPointer<QWidget> alive(focused);
                    const QString keyName = (key == Qt::Key_Up) ? "Up"
                        : (key == Qt::Key_Down) ? "Down"
                        : (key == Qt::Key_PageUp) ? "PageUp"
                        : "PageDown";
                    QTimer::singleShot(0, [alive, keyName, type] {
                        if (!alive) {
                            LogLine(QStringLiteral(
                                "Arrow %1 on %2 -> widget gone")
                                .arg(keyName, type));
                            return;
                        }
                        QString summary = QStringLiteral(
                            "Arrow %1 on %2 hasFocus=%3 isVisible=%4")
                            .arg(keyName, type)
                            .arg(alive->hasFocus() ? 1 : 0)
                            .arg(alive->isVisible() ? 1 : 0);
                        if (auto *iface = QAccessible::queryAccessibleInterface(
                                alive.data())) {
                            summary += QStringLiteral(
                                " childCount=%1").arg(iface->childCount());
                            if (auto *child = iface->focusChild()) {
                                const auto name = child->text(
                                    QAccessible::Name);
                                summary += QStringLiteral(
                                    " focusChild.name=\"%1\" role=%2")
                                    .arg(name)
                                    .arg(int(child->role()));
                                // The MSAA child-id path doesn't make
                                // NVDA speak even though everything is
                                // wired up correctly (confirmed via
                                // tg_a11y_diag.txt). Speak the row name
                                // directly through NVDA Controller
                                // Client — the documented IPC channel —
                                // so the user actually hears it.
                                nvda::Speak(name);
                            } else {
                                summary += QStringLiteral(
                                    " focusChild=nullptr");
                            }
                        } else {
                            summary += QStringLiteral(" iface=nullptr");
                        }
                        LogLine(summary);
                    });
                }
            }
        }
        // Ctrl+Shift+R — voice-record shortcut for screen reader users.
        // VoiceRecordButton is fundamentally hold-to-record, so neither a
        // synthesized click nor accessibilityDoAction("Press") starts the
        // capture. Our patch on VoiceRecordBar exposes a Q_INVOKABLE
        // accessibilityToggleRecord() that flips between
        //   no recording -> startRecordingAndLock(false)
        //   recording locked -> stop(true) (i.e. send)
        //   listen state    -> requestToSendWithOptions({})
        // so a single hotkey covers the whole flow.
        if (key == Qt::Key_R
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            if (QWidget *root = detail::FindMainWindow()) {
                QWidget *vrb = detail::FindByTypeAny(
                    root, "HistoryView::Controls::VoiceRecordBar");
                if (vrb) {
                    const bool ok = QMetaObject::invokeMethod(
                        vrb, "accessibilityToggleRecord",
                        Qt::DirectConnection);
                    LogLine(QStringLiteral(
                        "Ctrl+Shift+R -> VoiceRecordBar.accessibilityToggleRecord"
                        " invoked=%1").arg(ok ? 1 : 0));
                    return true;
                }
                LogLine(QStringLiteral(
                    "Ctrl+Shift+R -> VoiceRecordBar not found in main window"));
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
            LogLine(QStringLiteral(
                "F6 -> no panels discovered (typeid-based lookup "
                "found no Dialogs::Widget / HistoryWidget / "
                "Ui::InputField in the main window)"));
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
        const QString line = QStringLiteral(
            "F6 -> name=\"%1\"  type=\"%2\"  className=\"%3\"")
            .arg(name,
                 detail::DynamicTypeName(w),
                 QString::fromUtf8(w->metaObject()->className()));
        qDebug().noquote() << "[TgAccessibility]" << line;
        LogLine(line);
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
