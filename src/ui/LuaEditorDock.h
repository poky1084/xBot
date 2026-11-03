#pragma once

#include "bot/ScriptStrategy.h"

#include <QDockWidget>
#include <QList>
#include <QPair>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTextDocument;
class ScriptCodeEdit;
class LuaHighlighter;
class JsHighlighter;

// Dockable strategy editor for Lua and JavaScript: syntax-highlighted code view with
// Run / Stop, open / save, a language switch and a short API reference. Each language
// keeps its own script, undo history and file.
class LuaEditorDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit LuaEditorDock(QWidget *parent = nullptr);

    // The language currently shown; switching keeps both scripts.
    ScriptLanguage language() const;
    void setLanguage(ScriptLanguage language);

    // Script of the current language.
    QString script() const;
    void setScript(const QString &text);

    void setDark(bool dark);
    // Run is disabled and Stop enabled while a run is active (the language is locked too).
    void setRunning(bool running);
    // Shows an error (red) or a neutral message; an empty text clears the line.
    void setStatus(const QString &text, bool isError);
    // Console tab: print()/log() output (neutral) and script errors (red), timestamped.
    void appendConsole(const QString &text, bool isError);
    void clearConsole();

    // The owning main window's QSettings group (see ui/WindowSettings.h); set before loadSettings().
    void setSettingsGroup(const QString &group) { m_settingsGroup = group; }
    void loadSettings();
    void saveSettings() const;

    static QString defaultScript(ScriptLanguage language = ScriptLanguage::Lua);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override; // command line Up / Down history

signals:
    void runRequested(const QString &script, ScriptLanguage language);
    void stopRequested();
    // Console tab's command line: a Lua / JavaScript snippet for the running script.
    void commandRequested(const QString &code);

private slots:
    void onOpen();
    void onSave();
    void onSaveAs();
    void onHelp();

private:
    bool saveTo(const QString &path);
    void updateTitle();
    void applyLanguageUi();
    void writeConsoleLine(const QString &text, bool isError);

    ScriptCodeEdit *m_edit = nullptr;
    LuaHighlighter *m_luaHighlighter = nullptr;
    JsHighlighter *m_jsHighlighter = nullptr;
    QTextDocument *m_docs[2] = {nullptr, nullptr};   // [0] Lua, [1] JavaScript
    QString m_filePaths[2];                          // file behind each document
    ScriptLanguage m_lang = ScriptLanguage::Lua;
    QString m_settingsGroup;
    QComboBox *m_langBox = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_console = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QStringList m_commandHistory; // Up / Down in the command line
    int m_historyPos = -1;
    void sendCommand();
    QList<QPair<QString, bool>> m_consoleLines; // (text, isError), kept to re-colour on theme change
    bool m_dark = false;
    QAction *m_actRun = nullptr;
    QAction *m_actStop = nullptr;
};
