#pragma once

// Per-window settings.
//
// Every xBot main window keeps its own copy of all its data (window layout, Options,
// tokens, auto-bet, conditions, scripts, Provably Fair inputs …) in its own QSettings
// group (registry key), "Windows/<slot>": 0 = the main window, 1 = the first window
// opened with "+", 2 = the next one, … The app starts with window 0; "+" opens the
// lowest number that is not open and loads that window's saved data.

#include <QSettings>
#include <QString>

namespace WindowSettings
{
inline QString groupFor(int slot)
{
    return QStringLiteral("Windows/%1").arg(slot);
}
} // namespace WindowSettings

// A QSettings that reads and writes inside one window's group. An empty group means
// the root (used by code that has not been given a window yet).
class ScopedSettings : public QSettings
{
public:
    explicit ScopedSettings(const QString &group)
    {
        if (!group.isEmpty())
            beginGroup(group);
    }
};
