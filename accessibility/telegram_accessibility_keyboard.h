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

// HMODULE for the loaded controllerClient DLL. Tracked so we can
// FreeLibrary it and reload — necessary when NVDA restarts under us
// (the DLL's internal RPC binding gets stranded on the old NVDA's
// pipe, so every speakText call silently returns RPC_S_CALL_FAILED
// until we re-init the DLL).
inline HMODULE &DllHandle() { static HMODULE h = nullptr; return h; }

// Set by Install() to false after Init/EnsureLoaded has had its
// first pass. Reset to false by Reload() so the next Speak() call
// re-runs the full search-and-load sequence.
inline bool &LoadAttempted() { static bool t = false; return t; }

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
    if (LoadAttempted()) return;
    LoadAttempted() = true;

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

    // The application's own directory is where our installer drops
    // nvdaControllerClient.dll. We try it FIRST and via an explicit
    // full path because Qt's startup code calls
    //   SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)
    // for security — that restricts subsequent bare LoadLibraryW()
    // calls to System32 only, so the bundled DLL won't be found via
    // the "default" search even though it sits right next to
    // Telegram.exe. Confirmed in the user log:
    //   [nvda] try default search path (nvdaControllerClient.dll)
    //       -> not found
    // with the DLL definitely present.
    HMODULE dll = nullptr;
    {
        wchar_t exePath[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring exeDir(exePath, len);
            const auto slash = exeDir.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                exeDir.resize(slash);
                dll = tryLoadAt(
                    QStringLiteral("application directory"),
                    exeDir);
            }
        }
    }

    // Default-search fallback. Mostly redundant given the explicit
    // application-dir attempt above, but cheap and covers a future
    // case where the DLL is on PATH or already loaded.
    if (!dll) {
        dll = tryLoadAt(QStringLiteral("default search path"), L"");
    }

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
        DllHandle() = dll;
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

// Free the DLL and reset state so the next Speak() call re-loads from
// scratch. Needed when NVDA restarts: the controllerClient DLL keeps
// an internal RPC binding to the (now-dead) old NVDA process, and
// every subsequent speakText() silently returns RPC_S_CALL_FAILED
// against the stale pipe. Reload forces DllMain's PROCESS_DETACH
// (clearing the binding) and the next LoadLibraryW gets a fresh one
// pointing at the running NVDA.
inline void Reload() {
    if (HMODULE h = DllHandle()) {
        FreeLibrary(h);
    }
    DllHandle() = nullptr;
    SpeakPtr() = nullptr;
    CancelPtr() = nullptr;
    LoadAttempted() = false;
    LogLine(QStringLiteral(
        "[nvda] reloading controller client DLL (NVDA may have "
        "restarted)"));
    EnsureLoaded();
}

// One shot: call speakText. nvdaController_speakText already
// interrupts any in-progress NVDA speech and replaces it with the
// new text, so a separate cancelSpeech() call is redundant and
// turned out to be actively harmful: when the user holds an arrow
// key (auto-repeat) every keypress fires cancel+speak in quick
// succession, killing NVDA's speech before it can finish even the
// first syllable. The log showed eight identical
//   [nvda] speakText(200 chars) rc=0 first40="Channel, Rozetked..."
// lines in a row — NVDA accepted each one (rc=0) but heard nothing
// because each call wiped the previous queue mid-utterance.
inline long SpeakOnce(const QString &text) {
    if (auto speak = SpeakPtr()) {
        return speak(reinterpret_cast<const wchar_t *>(text.utf16()));
    }
    return -1; // pointer not available
}

inline QString &LastSpokenText() {
    static QString last;
    return last;
}

inline void Speak(const QString &text) {
    EnsureLoaded();
    if (text.isEmpty()) return;

    // Dedupe consecutive identical text. Holding an arrow key past
    // the end of the list (or any other case where the focused row
    // doesn't change) used to issue dozens of speakText calls per
    // second for the same string; NVDA cancels its own speech to
    // start the "new" one, so the user heard a long stutter or
    // nothing at all. If text hasn't changed, do nothing — NVDA will
    // finish reading what it's already saying.
    if (text == LastSpokenText()) {
        return;
    }
    LastSpokenText() = text;

    // nvdaController_speakText returns error_status_t (a 32-bit RPC code).
    // 0 == success. Anything else means NVDA didn't actually speak the
    // text. Common values:
    //   1722 (RPC_S_SERVER_UNAVAILABLE) — NVDA process isn't listening
    //   1726 (RPC_S_CALL_FAILED) — IPC pipe broke mid-call
    //   1727 (RPC_S_CALL_FAILED_DNE) — endpoint mapper rejection
    //   1717 (RPC_S_UNKNOWN_IF) — DLL ABI mismatch vs the running NVDA
    // On rc != 0, reload the DLL (NVDA likely restarted, stranding
    // our RPC binding) and retry once.
    long rc = SpeakOnce(text);

    bool reloaded = false;
    if (rc != 0 && rc != -1) {
        LogLine(QStringLiteral(
            "[nvda] speakText rc=%1 — reloading DLL").arg(rc));
        Reload();
        reloaded = true;
        rc = SpeakOnce(text);
    }

    LogLine(QStringLiteral(
        "[nvda] speakText(%1 chars)%2 rc=%3 first40=\"%4\"")
        .arg(text.size())
        .arg(reloaded
                ? QStringLiteral(" [after reload]")
                : QString())
        .arg(rc)
        .arg(text.left(40).replace(QChar('\n'), QChar(' '))));
}

// Speak without the LastSpokenText dedupe AND truncate very long
// strings before handing them to nvdaController_speakText. Used for
// the chat-list arrow handler: empirically NVDA's controllerClient
// path returns rc=0 for any string length but the user hears nothing
// when the payload is ~180+ chars (each row in Dialogs::InnerWidget
// contains type + name + mute + unread count + last sender +
// preview + direction + timestamp, easily >200 chars). Shorter
// strings pass through; cap aggressively so we always stay under
// whatever NVDA's internal limit turns out to be.
inline void SpeakForced(const QString &text) {
    EnsureLoaded();
    if (text.isEmpty()) return;

    // Truncate but keep the meaningful prefix. NVDA reads the chat
    // title first in the lib_ui-built string, so the first ~110
    // chars are the most informative bit.
    QString trimmed = text;
    constexpr int kMaxChars = 110;
    if (trimmed.size() > kMaxChars) {
        trimmed = trimmed.left(kMaxChars) + QStringLiteral("…");
    }

    long rc = SpeakOnce(trimmed);
    bool reloaded = false;
    if (rc != 0 && rc != -1) {
        Reload();
        reloaded = true;
        rc = SpeakOnce(trimmed);
    }
    LastSpokenText() = trimmed;
    LogLine(QStringLiteral(
        "[nvda] speakTextForced(%1 chars, orig=%2)%3 rc=%4 "
        "first40=\"%5\"")
        .arg(trimmed.size())
        .arg(text.size())
        .arg(reloaded
                ? QStringLiteral(" [after reload]")
                : QString())
        .arg(rc)
        .arg(trimmed.left(40).replace(QChar('\n'), QChar(' '))));
}

// Startup self-test. Speaks one fixed phrase shortly after the DLL is
// loaded. Diagnostic intent:
//   * If the user HEARS it -> nvdaController_speakText genuinely
//     produces audio on their NVDA; any remaining "silent on arrow
//     keys" problem is navigation-specific (queueing, dedupe,
//     event-timing) and we keep digging there.
//   * If the user does NOT hear it but the log shows rc=0 -> NVDA
//     accepts the RPC but never voices it. That points at NVDA-side
//     state (speech mode, sleep mode, synth) rather than our code,
//     and we'd switch strategy entirely.
// Bypasses the LastSpokenText() dedupe on purpose — it's a one-shot.
inline void SelfTest() {
    EnsureLoaded();
    const auto phrase = QStringLiteral(
        "Специальные возможности Telegram загружены.");
    long rc = SpeakOnce(phrase);
    if (rc != 0 && rc != -1) {
        Reload();
        rc = SpeakOnce(phrase);
    }
    LogLine(QStringLiteral(
        "[nvda] SELF-TEST speakText rc=%1 — if you did not hear "
        "the test phrase, controllerClient speech is not reaching "
        "NVDA's synthesizer despite the success code").arg(rc));
}

#else // Q_OS_WIN

inline void SelfTest() {}

inline void Speak(const QString &) {}

inline void SpeakForced(const QString &) {}

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

        // Never hijack keys while a popup menu or modal dialog is open.
        //
        // Bug this fixes: opening a message context menu with Shift+F10
        // and then pressing Escape did not close it. Our Escape handler
        // below intercepts Escape globally and redirects focus to the
        // chat list, returning true — so the event was consumed before
        // the Ui::PopupMenu ever saw it. The menu stayed open (just lost
        // visible focus) and the next arrow key "revived" it.
        //
        // While a popup/modal is up the user is interacting with THAT;
        // F6 panel-cycling, arrow announcements and our Escape redirect
        // all make no sense there. Let every key fall through untouched.
        if (QApplication::activePopupWidget()
            || QApplication::activeModalWidget()) {
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
                                //
                                // For chats: bypass dedupe AND truncate.
                                // The lib_ui-built chat name strings
                                // are very long (~180-200 chars). They
                                // come back rc=0 from speakText but
                                // produce no audio — strongly suggests
                                // an internal NVDA controllerClient
                                // length limit that just drops the
                                // call silently. Messages go through
                                // the normal Speak() since their names
                                // are short enough.
                                if (type == QLatin1String(
                                        "Dialogs::InnerWidget")) {
                                    nvda::SpeakForced(name);

                                    // Last resort: smuggle the chat name
                                    // into NVDA via the focused widget's
                                    // OWN accessible name + a NameChanged
                                    // event. NVDA reliably re-reads the
                                    // focused widget's name when it sees
                                    // EVENT_OBJECT_NAMECHANGE on it — this
                                    // bypasses both the dead controller
                                    // client and the child-id dedup that
                                    // suppresses our per-row Focus events
                                    // in the chat list. We confirmed the
                                    // path works for HistoryInner via
                                    // MSAA focus, and that NVDA+Shift+M
                                    // can read the chat list focus,
                                    // meaning NVDA has the data — it just
                                    // refuses to auto-announce child-id
                                    // changes on Dialogs::InnerWidget.
                                    // NameChanged on the focused parent
                                    // is the one event NVDA can\'t dedup
                                    // away, since each new chat name is
                                    // a fresh string.
                                    QString shortName = name;
                                    constexpr int kMax = 110;
                                    if (shortName.size() > kMax) {
                                        shortName = shortName.left(kMax)
                                            + QStringLiteral("…");
                                    }
                                    alive->setAccessibleName(shortName);
                                    QAccessibleEvent nameEv(alive.data(),
                                        QAccessible::NameChanged);
                                    QAccessible::updateAccessibility(
                                        &nameEv);
                                } else {
                                    nvda::Speak(name);
                                }
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
        // Ctrl+Shift+T — re-trigger the speech self-test on demand so
        // the user can confirm whether NVDA's controllerClient path
        // produces audio in the *current* application state (not just
        // at startup). If pressing this is silent BUT NVDA reads
        // other things, then the controller path is dead and we have
        // to rely purely on MSAA focus events.
        if (key == Qt::Key_T
            && (mods & Qt::ControlModifier)
            && (mods & Qt::ShiftModifier)) {
            nvda::SelfTest();
            return true;
        }

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

        // After an app restart NVDA sometimes keeps a stale view of this
        // panel: it enumerated the accessibility tree before lib_ui
        // finished wiring it (or it cached the previous session's HWND
        // children list) and never re-reads it on its own. Confirmed
        // empirically: arrow keys then fire the right Focus/Selection
        // events on Dialogs::InnerWidget (childCount=41, focusChild.name
        // is correct) but NVDA stays silent — meanwhile HistoryInner's
        // identical event burst is announced fine.
        //
        // Fire a layered burst on THIS panel only (not the window root,
        // which is what broke focus tracking previously). Each event
        // invalidates a different layer of NVDA's cache:
        //   ObjectShow    — tells NVDA the widget exists now / is visible
        //   ObjectReorder — tells NVDA its child list changed
        //   NameChanged   — tells NVDA to re-read the panel name
        //   Focus (later) — sent by setFocus + manual event below
        {
            QAccessibleEvent show(w, QAccessible::ObjectShow);
            QAccessible::updateAccessibility(&show);
            QAccessibleEvent reorder(w, QAccessible::ObjectReorder);
            QAccessible::updateAccessibility(&reorder);
            QAccessibleEvent renamed(w, QAccessible::NameChanged);
            QAccessible::updateAccessibility(&renamed);
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
