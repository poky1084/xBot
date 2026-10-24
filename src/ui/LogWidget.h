#pragma once

#include <QColor>
#include <QTableWidget>

// Session log shown as a table: Time | Level | Message, one row per entry,
// coloured by level. Auto-scrolls only while you are at the bottom, so you can
// scroll up and read without being yanked back down.
class LogWidget : public QTableWidget
{
    Q_OBJECT

public:
    enum class Level { Info, Success, Warning, Error };

    explicit LogWidget(QWidget *parent = nullptr);

    // Text colour and row tint for a level. An invalid QColor means "use the
    // theme default" (foreground) / "no tint" (background).
    void setLevelColors(Level level, const QColor &foreground, const QColor &rowTint = QColor());

public slots:
    // Same signature the rest of the app already uses; the level is derived
    // from isError and the message text (see classify()).
    void appendLog(const QString &message, bool isError);
    // Explicit level.
    void appendEntry(const QString &message, Level level);
    void clearLog();
    void copySelected();

private:
    struct Style
    {
        QColor fg;
        QColor bg;
    };

    static Level classify(const QString &message, bool isError);
    static QString levelText(Level level);
    void trimOldRows();

    Style m_styles[4];
};
