#pragma once

// WebView2 browsing sessions per main window.
//
// The first main window uses WebView2's default user-data folder, so its site logins
// (cookies, local storage) persist between launches as before. A window opened with the
// "+" button gets its own, brand-new user-data folder instead: every WebView2 window it
// opens (hidden Stake browser, Shuffle / Goated / Wolfbet login windows) starts logged
// out, and logging in there does not touch the other windows' sessions. Options-dialog
// data (QSettings) is shared as before.
//
// The folder is stored as a dynamic property on the main window; WebView2 dialogs look it
// up on themselves or their parent chain, so dialogs opened from that window (e.g. the
// login dialogs opened from the Options dialog) use it automatically.
//
// Fresh folders live in %LOCALAPPDATA%/<app>/WebView2Sessions/<pid>-<uuid>. They are
// removed when the app quits; anything left behind (crash, files still locked) is removed
// at the next start once the process that created it is gone.

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QUuid>
#include <QWidget>

#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX // keep std::min / std::max usable in files that include this header
#endif
#include <windows.h>
#endif

namespace WebViewSession
{
inline const char *kProperty = "webView2UserDataFolder";

inline QString sessionsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/WebView2Sessions");
}

inline bool processAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    Q_UNUSED(pid);
    return true; // can't tell: keep it
#endif
}

// Removes folders left by earlier runs whose process has exited. Done once per process.
inline void cleanupStale()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    QDir root(sessionsRoot());
    const qint64 self = QCoreApplication::applicationPid();
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = name.section(QLatin1Char('-'), 0, 0).toLongLong(&ok);
        if (!ok || pid == self || processAlive(pid))
            continue;
        QDir(root.filePath(name)).removeRecursively();
    }
}

// A new, empty user-data folder for one main window. Removed when the app quits
// (best effort: WebView2 may still hold files; then the next start removes it).
inline QString createFresh()
{
    cleanupStale();
    const QString path = sessionsRoot() + QLatin1Char('/')
        + QString::number(QCoreApplication::applicationPid()) + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(path);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp,
                     [path] { QDir(path).removeRecursively(); });
    return path;
}

// Folder for WebView2 windows owned by `w` (looked up on w and its parents).
// Empty = WebView2's default folder (the persistent, shared session).
inline QString folderFor(const QWidget *w)
{
    for (const QWidget *p = w; p; p = p->parentWidget()) {
        const QVariant v = p->property(kProperty);
        if (v.isValid())
            return v.toString();
    }
    return QString();
}

// Wide string for CreateCoreWebView2EnvironmentWithOptions(); keep it alive for the call.
// Use: auto f = WebViewSession::folderW(this); Create...(nullptr, f.empty() ? nullptr : f.c_str(), ...)
inline std::wstring folderW(const QWidget *w)
{
    const QString f = folderFor(w);
    return f.isEmpty() ? std::wstring() : QDir::toNativeSeparators(f).toStdWString();
}
} // namespace WebViewSession
