#pragma once

// Works around the WebView2 runtime bug (152.0.4191.x) that leaves the mouse pointer hidden
// over the whole app after a WebView2 window closes.
//
// What happens: the WebView2 content window (in msedgewebview2.exe) is a child of our window,
// so Windows attaches its input queue to our GUI thread and both share one cursor state. When
// "Hide pointer while typing" is on, the browser hides the pointer while you type the login
// details (ShowCursor(FALSE) / SetCursor(NULL)) and, with this runtime, never undoes it. When
// the login window closes, our thread keeps the "hidden" count, so the pointer stays invisible
// over every window of the app (it is visible again over other apps).
//
// restoreMouseCursor() brings the display count back to "shown", sets the arrow and nudges the
// pointer so Qt re-applies the right cursor. It never makes the count higher than "shown", so
// it cannot break a cursor the app hides on purpose (it does not hide any).
//
// Use: declare a CursorRestorer before the dialog that hosts a WebView2, e.g.
//     const CursorRestorer restoreCursor; // runs after `dlg` below is destroyed
//     SomeLoginDialog dlg(this);
//     dlg.exec();

#include <QCoreApplication>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

inline void restoreMouseCursor()
{
#ifdef Q_OS_WIN
    // Display count: shown when >= 0. Raise it to 0 if it is negative, keep it otherwise.
    int count = ShowCursor(TRUE);
    while (count < 0)
        count = ShowCursor(TRUE);
    if (count > 0)
        ShowCursor(FALSE); // it was already >= 0: undo our own increment

    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    // A real (1 px there and back) mouse move: ends "hidden while typing" and makes the window
    // under the pointer send WM_SETCURSOR, so Qt sets its own cursor again.
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dx = 1;
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dx = -1;
    in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(2, in, sizeof(INPUT));
#endif
}

// Restores the cursor now and again a little later: the browser process may hide it once more
// while its window is being torn down.
inline void restoreMouseCursorSoon()
{
    restoreMouseCursor();
    QObject *ctx = QCoreApplication::instance();
    if (!ctx)
        return;
    for (int ms : {0, 300, 1000})
        QTimer::singleShot(ms, ctx, [] { restoreMouseCursor(); });
}

// RAII: restores the cursor when it goes out of scope (after a WebView2 dialog declared below it
// has been destroyed).
class CursorRestorer
{
public:
    CursorRestorer() = default;
    ~CursorRestorer() { restoreMouseCursorSoon(); }
    CursorRestorer(const CursorRestorer &) = delete;
    CursorRestorer &operator=(const CursorRestorer &) = delete;
};
