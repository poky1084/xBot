#include "ui/LuaEditorDock.h"

#include <QAction>
#include <QComboBox>
#include <QListView>
#include <QStyledItemDelegate>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPlainTextDocumentLayout>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include "ui/WindowSettings.h"
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QToolBar>
#include <QVBoxLayout>

// ============================================================ highlighter

class LuaHighlighter : public QSyntaxHighlighter
{
public:
    explicit LuaHighlighter(QTextDocument *doc)
        : QSyntaxHighlighter(doc)
    {
        setDark(true);
    }

    void setDark(bool dark)
    {
        auto fmt = [](const char *color, bool bold = false, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(QColor(QLatin1String(color)));
            if (bold)
                f.setFontWeight(QFont::Bold);
            f.setFontItalic(italic);
            return f;
        };
        if (dark) {
            m_keyword = fmt("#c586c0", true);
            m_api = fmt("#4ec9b0");
            m_builtin = fmt("#9cdcfe");
            m_string = fmt("#ce9178");
            m_number = fmt("#b5cea8");
            m_comment = fmt("#6a9955", false, true);
            m_function = fmt("#dcdcaa");
        } else {
            m_keyword = fmt("#af00db", true);
            m_api = fmt("#267f99");
            m_builtin = fmt("#001080");
            m_string = fmt("#a31515");
            m_number = fmt("#098658");
            m_comment = fmt("#008000", false, true);
            m_function = fmt("#795e26");
        }
        rehighlight();
    }

protected:
    void highlightBlock(const QString &t) override
    {
        static const QSet<QString> keywords = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
            "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until",
            "while"};
        // Variables / functions the strategy API provides.
        static const QSet<QString> api = {
            "nextbet", "chance", "bethigh", "target", "game", "currency", "dobet", "stop", "startbalance", "balance",
            "profit", "win", "previousbet", "lastBet", "lastprofit", "roll", "bets", "wins", "losses", "wagered",
            "currentstreak", "log", "resetstats", "resetseed", "vault", "result",
            // Stake's other games (reference bot variables)
            "bet", "currentBet", "round", "currentprofit", "winstreak", "losestreak", "numbers",
            "risk", "fields", "mines", "rows", "segments", "startcard", "pattern", "chips", "eggs",
            "difficulty", "guesses", "pumps", "rolls", "lines", "tiles", "steps", "pick", "moles",
            "picks", "condition", "target1", "target2", "target3", "target4", "checkTargetHit",
            "HILO_BET_HIGH", "HILO_BET_LOW", "HILO_BET_EQUAL", "HILO_SKIP", "HILO_CASHOUT", "beep"};
        static const QSet<QString> builtins = {
            "print", "math", "string", "table", "os", "utf8", "coroutine", "pairs", "ipairs",
            "tonumber", "tostring", "type", "select", "error", "assert", "pcall", "next",
            "setmetatable", "getmetatable", "rawget", "rawset", "unpack"};

        const int n = t.size();
        int i = 0;
        int state = previousBlockState();
        setCurrentBlockState(0);

        // Continue a long comment / long string from the previous line.
        if (state > 0) {
            const int kind = state & 3;      // 1 = comment, 2 = string
            const int level = state >> 2;
            const int end = findLongClose(t, 0, level);
            const QTextCharFormat &f = kind == 1 ? m_comment : m_string;
            if (end < 0) {
                setFormat(0, n, f);
                setCurrentBlockState(state);
                return;
            }
            setFormat(0, end, f);
            i = end;
        }

        auto isIdStart = [](QChar c) { return c.isLetter() || c == QLatin1Char('_'); };
        auto isId = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };

        QString prevWord;
        while (i < n) {
            const QChar c = t.at(i);

            if (c == QLatin1Char('-') && i + 1 < n && t.at(i + 1) == QLatin1Char('-')) {
                const int level = longOpenLevel(t, i + 2);
                if (level >= 0) {
                    const int bodyStart = i + 2 + level + 2;
                    const int end = findLongClose(t, bodyStart, level);
                    if (end < 0) {
                        setFormat(i, n - i, m_comment);
                        setCurrentBlockState((level << 2) | 1);
                        return;
                    }
                    setFormat(i, end - i, m_comment);
                    i = end;
                    continue;
                }
                setFormat(i, n - i, m_comment);
                return;
            }

            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                int j = i + 1;
                while (j < n && t.at(j) != c) {
                    if (t.at(j) == QLatin1Char('\\'))
                        ++j;
                    ++j;
                }
                j = qMin(j + 1, n);
                setFormat(i, j - i, m_string);
                i = j;
                continue;
            }

            if (c == QLatin1Char('[')) {
                const int level = longOpenLevel(t, i);
                if (level >= 0) {
                    const int bodyStart = i + level + 2;
                    const int end = findLongClose(t, bodyStart, level);
                    if (end < 0) {
                        setFormat(i, n - i, m_string);
                        setCurrentBlockState((level << 2) | 2);
                        return;
                    }
                    setFormat(i, end - i, m_string);
                    i = end;
                    continue;
                }
            }

            const bool prevIsId = i > 0 && isId(t.at(i - 1));
            if (!prevIsId && (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && t.at(i + 1).isDigit()))) {
                int j = i;
                if (c == QLatin1Char('0') && i + 1 < n && (t.at(i + 1) == QLatin1Char('x') || t.at(i + 1) == QLatin1Char('X'))) {
                    j += 2;
                    while (j < n && (t.at(j).isDigit() || QStringLiteral("abcdefABCDEF").contains(t.at(j))))
                        ++j;
                } else {
                    while (j < n && (t.at(j).isDigit() || t.at(j) == QLatin1Char('.')))
                        ++j;
                    if (j < n && (t.at(j) == QLatin1Char('e') || t.at(j) == QLatin1Char('E'))) {
                        ++j;
                        if (j < n && (t.at(j) == QLatin1Char('+') || t.at(j) == QLatin1Char('-')))
                            ++j;
                        while (j < n && t.at(j).isDigit())
                            ++j;
                    }
                }
                setFormat(i, j - i, m_number);
                i = j;
                continue;
            }

            if (isIdStart(c)) {
                int j = i + 1;
                while (j < n && isId(t.at(j)))
                    ++j;
                const QString word = t.mid(i, j - i);
                const bool afterDot = i > 0 && t.at(i - 1) == QLatin1Char('.');
                int k = j;
                while (k < n && t.at(k).isSpace())
                    ++k;
                const bool isCall = k < n && t.at(k) == QLatin1Char('(');

                if (keywords.contains(word))
                    setFormat(i, j - i, m_keyword);
                else if (prevWord == QLatin1String("function"))
                    setFormat(i, j - i, m_function);
                else if (!afterDot && api.contains(word))
                    setFormat(i, j - i, m_api);
                else if (!afterDot && builtins.contains(word))
                    setFormat(i, j - i, m_builtin);
                else if (isCall)
                    setFormat(i, j - i, m_function);
                prevWord = word;
                i = j;
                continue;
            }

            if (!c.isSpace())
                prevWord.clear();
            ++i;
        }
    }

private:
    // "[" "="* "[" at pos -> level, otherwise -1.
    static int longOpenLevel(const QString &t, int pos)
    {
        if (pos >= t.size() || t.at(pos) != QLatin1Char('['))
            return -1;
        int j = pos + 1;
        int level = 0;
        while (j < t.size() && t.at(j) == QLatin1Char('=')) {
            ++level;
            ++j;
        }
        return (j < t.size() && t.at(j) == QLatin1Char('[')) ? level : -1;
    }

    // Index just past the matching "]" "="*level "]", or -1.
    static int findLongClose(const QString &t, int from, int level)
    {
        const QString close = QStringLiteral("]") + QString(level, QLatin1Char('=')) + QStringLiteral("]");
        const int idx = t.indexOf(close, from);
        return idx < 0 ? -1 : idx + close.size();
    }

    QTextCharFormat m_keyword, m_api, m_builtin, m_string, m_number, m_comment, m_function;
};

// Highlighter for the JavaScript strategy language. Block states: 0 = normal,
// 1 = inside a /* ... */ comment, 2 = inside a `template literal`.
class JsHighlighter : public QSyntaxHighlighter
{
public:
    explicit JsHighlighter(QTextDocument *doc)
        : QSyntaxHighlighter(doc)
    {
        setDark(true);
    }

    void setDark(bool dark)
    {
        auto fmt = [](const char *color, bool bold = false, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(QColor(QLatin1String(color)));
            if (bold)
                f.setFontWeight(QFont::Bold);
            f.setFontItalic(italic);
            return f;
        };
        if (dark) {
            m_keyword = fmt("#c586c0", true);
            m_api = fmt("#4ec9b0");
            m_builtin = fmt("#9cdcfe");
            m_string = fmt("#ce9178");
            m_number = fmt("#b5cea8");
            m_comment = fmt("#6a9955", false, true);
            m_function = fmt("#dcdcaa");
        } else {
            m_keyword = fmt("#af00db", true);
            m_api = fmt("#267f99");
            m_builtin = fmt("#001080");
            m_string = fmt("#a31515");
            m_number = fmt("#098658");
            m_comment = fmt("#008000", false, true);
            m_function = fmt("#795e26");
        }
        rehighlight();
    }

protected:
    void highlightBlock(const QString &t) override
    {
        static const QSet<QString> keywords = {
            "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete",
            "do", "else", "export", "extends", "false", "finally", "for", "function", "if", "import",
            "in", "instanceof", "let", "new", "null", "of", "return", "super", "switch", "this",
            "throw", "true", "try", "typeof", "undefined", "var", "void", "while", "with", "yield",
            "async", "await", "static", "get", "set"};
        // Variables / functions the strategy API provides.
        static const QSet<QString> api = {
            "nextbet", "chance", "bethigh", "target", "game", "currency", "dobet", "stop", "startbalance", "balance",
            "profit", "win", "previousbet", "lastBet", "lastprofit", "roll", "bets", "wins", "losses", "wagered",
            "currentstreak", "log", "print", "resetstats", "resetseed", "vault",
            // Stake's other games (reference bot variables)
            "bet", "currentBet", "round", "currentprofit", "winstreak", "losestreak", "numbers",
            "risk", "fields", "mines", "rows", "segments", "startcard", "pattern", "chips", "eggs",
            "difficulty", "guesses", "pumps", "rolls", "lines", "tiles", "steps", "pick", "moles",
            "picks", "condition", "target1", "target2", "target3", "target4", "checkTargetHit",
            "getOptimalHold", "getTargetedHold", "evaluateHand",
            "HILO_BET_HIGH", "HILO_BET_LOW", "HILO_BET_EQUAL", "HILO_SKIP", "HILO_CASHOUT", "beep"};
        static const QSet<QString> builtins = {
            "console", "Math", "JSON", "Date", "Object", "Array", "String", "Number", "Boolean",
            "Map", "Set", "Symbol", "Error", "RegExp", "parseInt", "parseFloat", "isNaN",
            "isFinite", "NaN", "Infinity"};

        const int n = t.size();
        int i = 0;
        const int state = previousBlockState();
        setCurrentBlockState(0);

        if (state == 1) { // continue a block comment
            const int end = t.indexOf(QLatin1String("*/"));
            if (end < 0) {
                setFormat(0, n, m_comment);
                setCurrentBlockState(1);
                return;
            }
            setFormat(0, end + 2, m_comment);
            i = end + 2;
        } else if (state == 2) { // continue a template literal
            const int end = templateEnd(t, 0);
            if (end < 0) {
                setFormat(0, n, m_string);
                setCurrentBlockState(2);
                return;
            }
            setFormat(0, end, m_string);
            i = end;
        }

        auto isIdStart = [](QChar c) { return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$'); };
        auto isId = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'); };

        QString prevWord;
        while (i < n) {
            const QChar c = t.at(i);

            if (c == QLatin1Char('/') && i + 1 < n && t.at(i + 1) == QLatin1Char('/')) {
                setFormat(i, n - i, m_comment);
                return;
            }
            if (c == QLatin1Char('/') && i + 1 < n && t.at(i + 1) == QLatin1Char('*')) {
                const int end = t.indexOf(QLatin1String("*/"), i + 2);
                if (end < 0) {
                    setFormat(i, n - i, m_comment);
                    setCurrentBlockState(1);
                    return;
                }
                setFormat(i, end + 2 - i, m_comment);
                i = end + 2;
                continue;
            }

            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                int j = i + 1;
                while (j < n && t.at(j) != c) {
                    if (t.at(j) == QLatin1Char('\\'))
                        ++j;
                    ++j;
                }
                j = qMin(j + 1, n);
                setFormat(i, j - i, m_string);
                i = j;
                continue;
            }

            if (c == QLatin1Char('`')) {
                const int end = templateEnd(t, i + 1);
                if (end < 0) {
                    setFormat(i, n - i, m_string);
                    setCurrentBlockState(2);
                    return;
                }
                setFormat(i, end - i, m_string);
                i = end;
                continue;
            }

            const bool prevIsId = i > 0 && isId(t.at(i - 1));
            if (!prevIsId && (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && t.at(i + 1).isDigit()))) {
                int j = i;
                const bool prefixed = c == QLatin1Char('0') && i + 1 < n
                    && QStringLiteral("xXbBoO").contains(t.at(i + 1));
                if (prefixed) {
                    j += 2;
                    while (j < n && (t.at(j).isLetterOrNumber() || t.at(j) == QLatin1Char('_')))
                        ++j;
                } else {
                    while (j < n && (t.at(j).isDigit() || t.at(j) == QLatin1Char('.')
                                     || t.at(j) == QLatin1Char('_')))
                        ++j;
                    if (j < n && (t.at(j) == QLatin1Char('e') || t.at(j) == QLatin1Char('E'))) {
                        ++j;
                        if (j < n && (t.at(j) == QLatin1Char('+') || t.at(j) == QLatin1Char('-')))
                            ++j;
                        while (j < n && t.at(j).isDigit())
                            ++j;
                    }
                    if (j < n && t.at(j) == QLatin1Char('n')) // BigInt
                        ++j;
                }
                setFormat(i, j - i, m_number);
                i = j;
                continue;
            }

            if (isIdStart(c)) {
                int j = i + 1;
                while (j < n && isId(t.at(j)))
                    ++j;
                const QString word = t.mid(i, j - i);
                const bool afterDot = i > 0 && t.at(i - 1) == QLatin1Char('.');
                int k = j;
                while (k < n && t.at(k).isSpace())
                    ++k;
                const bool isCall = k < n && t.at(k) == QLatin1Char('(');

                if (!afterDot && keywords.contains(word))
                    setFormat(i, j - i, m_keyword);
                else if (prevWord == QLatin1String("function") || prevWord == QLatin1String("class"))
                    setFormat(i, j - i, m_function);
                else if (!afterDot && api.contains(word))
                    setFormat(i, j - i, m_api);
                else if (!afterDot && builtins.contains(word))
                    setFormat(i, j - i, m_builtin);
                else if (isCall)
                    setFormat(i, j - i, m_function);
                prevWord = word;
                i = j;
                continue;
            }

            if (!c.isSpace())
                prevWord.clear();
            ++i;
        }
    }

private:
    // Index just past the closing backtick (skipping \-escapes), or -1 if the
    // template literal continues on the next line.
    static int templateEnd(const QString &t, int from)
    {
        for (int j = from; j < t.size(); ++j) {
            if (t.at(j) == QLatin1Char('\\'))
                ++j;
            else if (t.at(j) == QLatin1Char('`'))
                return j + 1;
        }
        return -1;
    }

    QTextCharFormat m_keyword, m_api, m_builtin, m_string, m_number, m_comment, m_function;
};

// ============================================================ code editor

class ScriptCodeEdit;

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(ScriptCodeEdit *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ScriptCodeEdit *m_editor;
};

class ScriptCodeEdit : public QPlainTextEdit
{
public:
    explicit ScriptCodeEdit(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_area(new LineNumberArea(this))
    {
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setFamilies({QStringLiteral("Consolas"), QStringLiteral("Cascadia Mono"), f.family()});
        f.setPointSizeF(10.0);
        setFont(f);
        setLineWrapMode(NoWrap);
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
        setFrameShape(QFrame::NoFrame);

        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateMargin(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
            if (dy)
                m_area->scroll(0, dy);
            else
                m_area->update(0, r.y(), m_area->width(), r.height());
            if (r.contains(viewport()->rect()))
                updateMargin();
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { highlightLine(); });
        updateMargin();
        highlightLine();
    }

    // Shows another buffer (the Lua or the JavaScript script) and adapts the
    // auto-indent rules to its language.
    void setScriptDocument(QTextDocument *doc, bool javascript)
    {
        m_js = javascript;
        if (document() != doc)
            setDocument(doc);
        updateMargin();
        highlightLine();
        m_area->update();
    }

    void setDark(bool dark)
    {
        m_dark = dark;
        setStyleSheet(dark ? QStringLiteral("QPlainTextEdit { background:#1e1e1e; color:#d4d4d4; "
                                            "selection-background-color:#264f78; }")
                           : QStringLiteral("QPlainTextEdit { background:#ffffff; color:#1f1f1f; "
                                            "selection-background-color:#add6ff; }"));
        highlightLine();
        m_area->update();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
            ++digits;
        return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void paintLineNumbers(QPaintEvent *event)
    {
        QPainter p(m_area);
        p.fillRect(event->rect(), m_dark ? QColor("#252526") : QColor("#f3f3f3"));
        p.setPen(m_dark ? QColor("#858585") : QColor("#237893"));

        QTextBlock block = firstVisibleBlock();
        int number = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top())
                p.drawText(0, top, m_area->width() - 6, fontMetrics().height(), Qt::AlignRight,
                           QString::number(number + 1));
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++number;
        }
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QPlainTextEdit::resizeEvent(e);
        const QRect cr = contentsRect();
        m_area->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Tab && !(e->modifiers() & Qt::ShiftModifier)
            && !textCursor().hasSelection()) {
            insertPlainText(QStringLiteral("    "));
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            // Keep the indentation of the current line.
            const QString line = textCursor().block().text();
            QString indent;
            for (const QChar c : line) {
                if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
                    indent += c;
                else
                    break;
            }
            // Indent one level deeper after a block opener: "then", "do", "else",
            // "function(...)" in Lua; "{", "(", "[" in JavaScript.
            const QString trimmed = line.trimmed();
            static const QRegularExpression luaOpener(
                QStringLiteral("(\\bthen|\\bdo|\\belse|\\brepeat|\\bfunction\\b.*\\))$"));
            static const QRegularExpression jsOpener(QStringLiteral("[\\{\\(\\[]\\s*(//.*)?$"));
            if ((m_js ? jsOpener : luaOpener).match(trimmed).hasMatch())
                indent += QStringLiteral("    ");
            QPlainTextEdit::keyPressEvent(e);
            insertPlainText(indent);
            return;
        }
        if (m_js && !textCursor().hasSelection()
            && (e->text() == QLatin1String("}") || e->text() == QLatin1String("]")
                || e->text() == QLatin1String(")"))) {
            // A closing bracket typed on an otherwise blank line steps back one level.
            QTextCursor cur = textCursor();
            const QString before = cur.block().text().left(cur.positionInBlock());
            if (before.trimmed().isEmpty() && before.endsWith(QLatin1String("    "))) {
                cur.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 4);
                cur.removeSelectedText();
                setTextCursor(cur);
            }
        }
        QPlainTextEdit::keyPressEvent(e);
    }

private:
    void updateMargin() { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

    void highlightLine()
    {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(m_dark ? QColor("#2a2d2e") : QColor("#f0f4fa"));
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        setExtraSelections({sel});
    }

    LineNumberArea *m_area;
    bool m_dark = true;
    bool m_js = false;
};

LineNumberArea::LineNumberArea(ScriptCodeEdit *editor)
    : QWidget(editor)
    , m_editor(editor)
{
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event)
{
    m_editor->paintLineNumbers(event);
}

// ================================================================== dock

namespace
{
constexpr int kLuaIndex = 0;
constexpr int kJsIndex = 1;

int indexOf(ScriptLanguage lang) { return lang == ScriptLanguage::JavaScript ? kJsIndex : kLuaIndex; }

// Replaces a document's text without leaving "undo" back to an empty editor.
void loadText(QTextDocument *doc, const QString &text)
{
    doc->setUndoRedoEnabled(false);
    doc->setPlainText(text);
    doc->setUndoRedoEnabled(true);
}
} // namespace

QString LuaEditorDock::defaultScript(ScriptLanguage language)
{
    if (language == ScriptLanguage::JavaScript) {
        return QStringLiteral(
            "game = \"dice\";   // \"dice\" or \"limbo\"\n"
            "nextbet = 0.0000000;\n"
            "chance = 49.5;   // dice\n"
            "bethigh = true;  // dice\n"
            "target = 2;      // used in limbo game\n"
            "basebet = nextbet;\n"
            "\n"
            "function dobet() {\n"
            "    if (win) {\n"
            "        nextbet = basebet;\n"
            "    } else {\n"
            "        nextbet = previousbet * 1;\n"
            "    }\n"
            "}\n");
    }
    return QStringLiteral(
        "game = \"dice\"   -- \"dice\" or \"limbo\"\n"
        "nextbet = 0.0000000\n"
        "chance = 49.5   -- dice\n"
        "bethigh = true  -- dice\n"
        "target = 2      -- used in limbo game\n"
        "basebet = nextbet\n"
        "\n"
        "function dobet()\n"
        "    if win then\n"
        "        nextbet = basebet\n"
        "    else\n"
        "        nextbet = previousbet * 1\n"
        "    end\n"
        "end\n");
}

LuaEditorDock::LuaEditorDock(QWidget *parent)
    : QDockWidget(tr("Lua code"), parent)
{
    setObjectName(QStringLiteral("luaDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);

    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QToolBar;
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setMovable(false);

    // Language switch: each language keeps its own script, undo history and file.
    m_langBox = new QComboBox;
    m_langBox->addItem(tr("Lua"));
    m_langBox->addItem(tr("JavaScript"));
    // Plain list popup + styled delegate so the stylesheet's ::item:hover/:selected
    // rules apply (Fusion's menu-style combo popup ignores them).
    {
        auto *view = new QListView(m_langBox);
        view->setMouseTracking(true);
        view->setMinimumWidth(110);
        m_langBox->setView(view);
        m_langBox->setItemDelegate(new QStyledItemDelegate(m_langBox));
    }
    m_langBox->setMinimumWidth(110);
    m_langBox->setToolTip(tr("Scripting language of the strategy. Each language keeps its own script."));
    connect(m_langBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        setLanguage(index == kJsIndex ? ScriptLanguage::JavaScript : ScriptLanguage::Lua);
    });
    bar->addWidget(m_langBox);
    bar->addSeparator();

    m_actRun = bar->addAction(QIcon(QStringLiteral(":/icons/torrent-start.svg")), tr("Run"));
    connect(m_actRun, &QAction::triggered, this, [this] { emit runRequested(script(), m_lang); });

    m_actStop = bar->addAction(QIcon(QStringLiteral(":/icons/torrent-stop.svg")), tr("Stop"));
    m_actStop->setToolTip(tr("Stop the running strategy"));
    m_actStop->setEnabled(false);
    connect(m_actStop, &QAction::triggered, this, &LuaEditorDock::stopRequested);

    bar->addSeparator();
    connect(bar->addAction(tr("Open…")), &QAction::triggered, this, &LuaEditorDock::onOpen);
    connect(bar->addAction(tr("Save")), &QAction::triggered, this, &LuaEditorDock::onSave);
    connect(bar->addAction(tr("Save as…")), &QAction::triggered, this, &LuaEditorDock::onSaveAs);
    bar->addSeparator();
    connect(bar->addAction(tr("Help")), &QAction::triggered, this, &LuaEditorDock::onHelp);
    layout->addWidget(bar);

    // Two tabs: the code editor (with its status line) and the console.
    auto *tabs = new QTabWidget;
    tabs->setTabPosition(QTabWidget::South);
    tabs->setDocumentMode(true);

    auto *editorPage = new QWidget;
    auto *editorLayout = new QVBoxLayout(editorPage);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    m_edit = new ScriptCodeEdit;
    // One document per language, each with its own highlighter.
    for (int i = 0; i < 2; ++i) {
        auto *doc = new QTextDocument(this);
        doc->setDocumentLayout(new QPlainTextDocumentLayout(doc));
        doc->setDefaultFont(m_edit->font());
        m_docs[i] = doc;
    }
    m_luaHighlighter = new LuaHighlighter(m_docs[kLuaIndex]);
    m_jsHighlighter = new JsHighlighter(m_docs[kJsIndex]);
    editorLayout->addWidget(m_edit, 1);

    m_status = new QLabel;
    m_status->setContentsMargins(8, 3, 8, 3);
    m_status->setWordWrap(true);
    m_status->hide();
    editorLayout->addWidget(m_status);

    auto *consolePage = new QWidget;
    auto *consoleLayout = new QVBoxLayout(consolePage);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(0);

    m_console = new QPlainTextEdit;
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(1000);
    m_console->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_console->setPlaceholderText(tr("print() / log() output and script errors appear here."));
    consoleLayout->addWidget(m_console, 1);

    auto *consoleBar = new QHBoxLayout;
    consoleBar->setContentsMargins(6, 3, 6, 3);
    auto *clearBtn = new QPushButton(tr("Clear"));
    connect(clearBtn, &QPushButton::clicked, this, &LuaEditorDock::clearConsole);
    consoleBar->addStretch();
    consoleBar->addWidget(clearBtn);
    consoleLayout->addLayout(consoleBar);

    tabs->addTab(editorPage, tr("Editor"));
    tabs->addTab(consolePage, tr("Console"));
    layout->addWidget(tabs, 1);

    setWidget(root);

    loadText(m_docs[kLuaIndex], defaultScript(ScriptLanguage::Lua));
    loadText(m_docs[kJsIndex], defaultScript(ScriptLanguage::JavaScript));
    m_edit->setScriptDocument(m_docs[kLuaIndex], false);
    setDark(false);
    applyLanguageUi();
}

ScriptLanguage LuaEditorDock::language() const
{
    return m_lang;
}

void LuaEditorDock::setLanguage(ScriptLanguage language)
{
    if (language == m_lang)
        return;
    m_lang = language;
    m_edit->setScriptDocument(m_docs[indexOf(language)], language == ScriptLanguage::JavaScript);
    setStatus({}, false); // an error shown for the other language is stale
    applyLanguageUi();
}

// Brings the combo box, title, tooltips and the editor in line with m_lang.
void LuaEditorDock::applyLanguageUi()
{
    {
        const QSignalBlocker blocker(m_langBox);
        m_langBox->setCurrentIndex(indexOf(m_lang));
    }
    m_actRun->setToolTip(tr("Start auto-bet with this %1 script").arg(scriptLanguageName(m_lang)));
    updateTitle();
}

QString LuaEditorDock::script() const
{
    return m_docs[indexOf(m_lang)]->toPlainText();
}

void LuaEditorDock::setScript(const QString &text)
{
    loadText(m_docs[indexOf(m_lang)], text);
}

void LuaEditorDock::setDark(bool dark)
{
    m_dark = dark;
    m_edit->setDark(dark);
    m_luaHighlighter->setDark(dark);
    m_jsHighlighter->setDark(dark);
    m_console->setStyleSheet(
        dark ? QStringLiteral("QPlainTextEdit { background:#1e1e1e; color:#d4d4d4; border:none; "
                              "selection-background-color:#264f78; }")
             : QStringLiteral("QPlainTextEdit { background:#ffffff; color:#1f1f1f; border:none; "
                              "selection-background-color:#add6ff; }"));

    // Lines already in the console carry the colours of the old theme (e.g. light
    // text on a now-white background); write them again with the new ones.
    m_console->clear();
    for (const auto &line : std::as_const(m_consoleLines))
        writeConsoleLine(line.first, line.second);
}

void LuaEditorDock::writeConsoleLine(const QString &text, bool isError)
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor(isError ? (m_dark ? "#ff6b6b" : "#c9372a")
                                     : (m_dark ? "#d4d4d4" : "#1f1f1f")));

    // Only follow the output while the view is already at the bottom. If the user has
    // scrolled up to read something, leave the scroll position alone.
    QScrollBar *bar = m_console->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;
    const int oldValue = bar->value();

    // Once the console is full (setMaximumBlockCount), adding a line drops the first one from
    // the top, which would pull everything up by that many lines under a view that is
    // scrolled up. Remember how many lines will go so the scroll position can follow.
    QTextDocument *doc = m_console->document();
    int droppedLines = 0;
    if (!atBottom && !doc->isEmpty() && doc->maximumBlockCount() > 0
        && doc->blockCount() >= doc->maximumBlockCount())
        droppedLines = qMax(1, doc->firstBlock().lineCount());

    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    if (!doc->isEmpty())
        cursor.insertBlock();
    cursor.insertText(isError ? tr("ERROR  %1").arg(text) : text, fmt);
    if (atBottom)
        bar->setValue(bar->maximum());
    else if (droppedLines > 0)
        bar->setValue(qMax(0, oldValue - droppedLines)); // keep the same text in view
}

void LuaEditorDock::appendConsole(const QString &text, bool isError)
{
    m_consoleLines.append({text, isError});
    while (m_consoleLines.size() > 1000) // same cap as the console's block count
        m_consoleLines.removeFirst();
    writeConsoleLine(text, isError);
}

void LuaEditorDock::clearConsole()
{
    m_consoleLines.clear();
    m_console->clear();
}

void LuaEditorDock::setRunning(bool running)
{
    m_actRun->setEnabled(!running);
    m_actStop->setEnabled(running);
    m_langBox->setEnabled(!running); // the run keeps the language it was started with
}

void LuaEditorDock::setStatus(const QString &text, bool isError)
{
    if (text.isEmpty()) {
        m_status->hide();
        return;
    }
    m_status->setStyleSheet(isError ? QStringLiteral("color:#ff6b6b;") : QString());
    m_status->setText(text);
    m_status->show();
}

void LuaEditorDock::updateTitle()
{
    const QString name = tr("%1 code").arg(scriptLanguageName(m_lang));
    const QString &file = m_filePaths[indexOf(m_lang)];
    setWindowTitle(file.isEmpty() ? name : tr("%1 — %2").arg(name, QFileInfo(file).fileName()));
}

void LuaEditorDock::onOpen()
{
    const QString &current = m_filePaths[indexOf(m_lang)];
    const QString dir = current.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(current).absolutePath();
    const QString filter = m_lang == ScriptLanguage::JavaScript
        ? tr("JavaScript (*.js);;Lua scripts (*.lua);;All files (*)")
        : tr("Lua scripts (*.lua);;JavaScript (*.js);;All files (*)");
    const QString path = QFileDialog::getOpenFileName(this, tr("Open script"), dir, filter);
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open script"), tr("Cannot read %1").arg(path));
        return;
    }
    // A .js / .lua file switches the editor to its language (not while a run is active:
    // the language is locked then and the file is loaded into the current one).
    if (m_langBox->isEnabled()) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QLatin1String("js"))
            setLanguage(ScriptLanguage::JavaScript);
        else if (suffix == QLatin1String("lua"))
            setLanguage(ScriptLanguage::Lua);
    }
    setScript(QString::fromUtf8(f.readAll()));
    m_filePaths[indexOf(m_lang)] = path;
    setStatus({}, false);
    updateTitle();
}

bool LuaEditorDock::saveTo(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save script"), tr("Cannot write %1").arg(path));
        return false;
    }
    f.write(script().toUtf8());
    m_filePaths[indexOf(m_lang)] = path;
    updateTitle();
    return true;
}

void LuaEditorDock::onSave()
{
    if (m_filePaths[indexOf(m_lang)].isEmpty())
        onSaveAs();
    else
        saveTo(m_filePaths[indexOf(m_lang)]);
}

void LuaEditorDock::onSaveAs()
{
    const QString &current = m_filePaths[indexOf(m_lang)];
    const QString dir = current.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QFileInfo(current).absolutePath();
    const bool js = m_lang == ScriptLanguage::JavaScript;
    QString path = QFileDialog::getSaveFileName(this, tr("Save %1 script").arg(scriptLanguageName(m_lang)),
                                                dir,
                                                js ? tr("JavaScript (*.js)") : tr("Lua scripts (*.lua)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += js ? QStringLiteral(".js") : QStringLiteral(".lua");
    saveTo(path);
}

void LuaEditorDock::onHelp()
{
    const bool js = m_lang == ScriptLanguage::JavaScript;
    const QString common = tr(
		"<code>casino</code> = <code>\"stake\"</code>, <code>\"shuffle\"</code>, <code>\"goated\"</code>, <code>\"wolfbet\"</code>, <code>\"duel\"</code>, <code>\"paradice\"</code>, <code>\"pasino\"</code>, <code>\"tower\"</code> or <code>\"winna\"</code> (set casino to play on)<br><br>"
        "<code>game</code> – <code>\"dice\"</code>, <code>\"limbo\"</code> or a Stake game below (default: the game "
        "selected in the Game menu); can be changed at any time, e.g. in <code>dobet()</code><br>"
        "<code>currency</code> – currency code to bet in, e.g. <code>\"btc\"</code> or "
        "<code>\"usdt\"</code> (default: the current bet currency); can be changed at any time. "
        "Live: the account needs a balance in it; the session is kept and only the balance "
        "becomes the new currency's<br>"
        "<code>nextbet</code> – amount of the next bet<br>"
        "<b>Dice:</b><br>"
        "<code>chance</code> – win chance in % (0.01 – 99.99)<br>"
        "<code>bethigh</code> – true = roll over, false = roll under<br>"
        "<b>Limbo:</b><br>"
        "<code>target</code> – multiplier target (1.01 – 1000000); <code>chance</code> and "
        "<code>bethigh</code> are not used<br>"
        "<b>Stake's other games</b> (Stake only; set <code>game</code> and <code>nextbet</code>):<br>"
        "<code>keno</code>: numbers (1 – 40), risk · <code>mines</code>: fields (1 – 25, 1 = top-left), mines · "
        "<code>plinko</code>: rows, risk · <code>wheel</code>: segments, risk · "
        "<code>tower</code>: eggs (1-based, one per floor), difficulty · <code>flip</code> / <code>rps</code>: guesses · "
        "<code>pump</code>: pumps, difficulty · <code>snakes</code>: rolls, difficulty · "
        "<code>cases</code> / <code>darts</code> / <code>tarot</code>: difficulty · "
        "<code>bars</code>: tiles, difficulty · <code>chicken</code>: steps, difficulty · <code>goatrun</code> (Goated): steps, difficulty · "
        "<code>drill</code>: target, pick (1 – 3) · <code>moles</code>: moles, picks (1-based) · "
        "<code>scarabspin</code> / <code>tomeoflife</code>: lines · "
        "<code>primedice</code>: condition, target1 – target4 · "
        "<code>videopoker</code>: target (hold combos or null) · "
        "<code>diamonds</code>, <code>packs</code>, <code>bluesamurai</code>: nothing else<br>"
        "<code>baccarat</code>: nextbet = {player, banker, tie} · "
        "<code>roulette</code>: chips = list of {value, amount} (nextbet not needed)<br>"
        "<code>hilo</code>: startcard = {rank, suit} and <code>round()</code>, which returns HILO_BET_HIGH / "
        "HILO_BET_LOW / HILO_BET_EQUAL / HILO_SKIP / HILO_CASHOUT · "
        "<code>blackjack</code>: <code>round()</code> returns \"BLACKJACK_HIT\" / \"_STAND\" / "
        "\"_DOUBLE\" / \"_SPLIT\" / \"_INSURANCE\" / \"_NOINSURANCE\"; in round(), "
        "<code>currentBet</code> is the open bet. Hilo and blackjack scripts must define round().<br>"
        "<b>No defaults in code mode:</b> every variable of the game must be set (video poker "
        "<code>target</code> may be nil / null = optimal hold), otherwise the run stops with the "
        "missing names.<br><br>"
        "<b>Read only</b><br>"
        "<code>win, previousbet, lastprofit, currentprofit, roll, balance, profit, bets, wins, "
        "losses, wagered, currentstreak, winstreak, losestreak, startbalance</code><br>"
        "<code>lastBet.amount, lastBet.roll (lastBet.Roll), lastBet.profit, lastBet.payout, "
        "lastBet.payoutMultiplier, lastBet.win, lastBet.id, lastBet.result</code> – details of the last bet; "
        "<code>lastBet.result</code> is the game's own result (baccarat \"banker\", roulette's number, the dice roll, ...) "
        "(<code>lastBet.roll</code> / <code>lastBet.Roll</code>: video poker the hand's outcome, e.g. "
        "<code>\"twoPair\"</code>; mines the mine positions, keno the drawn numbers, both 1-based lists)<br>"
        "<code>bet</code> – Stake's full bet object of the last bet (e.g. "
        "<code>bet.state.playerHand</code>); <code>checkTargetHit(hand, combos)</code> for video "
        "poker<br><br>"
        "<b>Functions</b><br>"
        "<code>dobet()</code> – define it; called after every bet<br>"
        "<code>stop(\"reason\")</code> – stop the run<br>"
        "<code>print(...)</code> / <code>log(...)</code> – write to the Console tab<br>"
        "<code>resetstats()</code> – reset the session statistics (bets, wins, losses, wagered, "
        "profit, streaks); the balance is kept<br>"
        "<code>resetseed([clientseed])</code> – rotate the seed pair, e.g. <code>resetseed(\"123xcv123\")</code>; "
        "without an argument a random 10-character alphanumeric client seed is used (live: waits for a bet "
        "in flight, the next bet waits for the server)<br>"
        "<code>vault(amount)</code> – deposit <code>amount</code> of the current currency into the vault, "
        "e.g. <code>vault(0.001)</code> (live mode)<br>"
        "<code>beep()</code> / <code>ding()</code> – play a bell tone (the reference bot's beep, e.g. when a target hits)");

    QString text = common;
    if (js) {
        text += tr(" (<code>console.log(...)</code> works too; objects are printed as JSON)<br><br>"
                   "<b>Syntax</b><br>"
                   "Plain JavaScript (ECMAScript). Set the values above as global variables: "
                   "<code>nextbet = 0.0001;</code> or <code>var nextbet = 0.0001;</code>. A top-level "
                   "<code>let</code> / <code>const</code> is not visible to the bot, so use it only "
                   "inside functions.<br><br>");
    } else {
        text += tr("<br><br>"
                   "<b>Syntax</b><br>"
                   "<code>!</code> works like <code>not</code> (e.g. <code>bethigh = !bethigh</code>) and "
                   "<code>!=</code> like <code>~=</code>. The compound assignments <code>+=</code>, "
                   "<code>-=</code>, <code>*=</code>, <code>/=</code> (also <code>//=</code>, "
                   "<code>%=</code>, <code>^=</code>, <code>..=</code>) work too: "
                   "<code>nextbet *= 2</code> means <code>nextbet = nextbet * (2)</code>. Put one "
                   "such statement per line (or end it with <code>;</code>) and keep the left side "
                   "free of function calls, because it is evaluated twice.<br><br>");
    }
    
    QMessageBox::information(this, tr("%1 strategy reference").arg(scriptLanguageName(m_lang)), text);
}

void LuaEditorDock::loadSettings()
{
    ScopedSettings s(m_settingsGroup);
    // "Lua/*" keys are the ones earlier versions wrote, so existing scripts survive.
    const QString lua = s.value(QStringLiteral("Lua/script")).toString();
    if (!lua.isEmpty())
        loadText(m_docs[kLuaIndex], lua);
    m_filePaths[kLuaIndex] = s.value(QStringLiteral("Lua/file")).toString();

    const QString js = s.value(QStringLiteral("JavaScript/script")).toString();
    if (!js.isEmpty())
        loadText(m_docs[kJsIndex], js);
    m_filePaths[kJsIndex] = s.value(QStringLiteral("JavaScript/file")).toString();

    setLanguage(s.value(QStringLiteral("Editor/language")).toString() == QLatin1String("javascript")
                    ? ScriptLanguage::JavaScript
                    : ScriptLanguage::Lua);
    updateTitle();
}

void LuaEditorDock::saveSettings() const
{
    ScopedSettings s(m_settingsGroup);
    s.setValue(QStringLiteral("Lua/script"), m_docs[kLuaIndex]->toPlainText());
    s.setValue(QStringLiteral("Lua/file"), m_filePaths[kLuaIndex]);
    s.setValue(QStringLiteral("JavaScript/script"), m_docs[kJsIndex]->toPlainText());
    s.setValue(QStringLiteral("JavaScript/file"), m_filePaths[kJsIndex]);
    s.setValue(QStringLiteral("Editor/language"),
               m_lang == ScriptLanguage::JavaScript ? QStringLiteral("javascript")
                                                    : QStringLiteral("lua"));
}
