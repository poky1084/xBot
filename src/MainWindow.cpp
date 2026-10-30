#include "MainWindow.h"

#include "bot/BotEngine.h"
#include "bot/SimulatedProvider.h"
#include "ui/AutoBetPanel.h"
#include "ui/BalanceGraphWidget.h"
#include "ui/BetHistoryModel.h"
#include "ui/ConditionBuilderDialog.h"
#include "ui/FairPanel.h"
#include "ui/LogWidget.h"
#include "ui/LuaEditorDock.h"
#include "ui/ManualBetPanel.h"
#include "ui/OptionsDialog.h"
#include "ui/WebViewSession.h"
#include "ui/WindowSettings.h"
#include "bot/GoatedTransport.h"
#include "bot/DuelTransport.h"
#include "ui/DuelBrowser.h"
#include "bot/WolfbetTransport.h"
#include "bot/ParadiceTransport.h"
#include "bot/PasinoTransport.h"
#include "bot/TowerTransport.h"
#include "bot/WinnaTransport.h"
#include "bot/ShuffleTransport.h"
#include "bot/StakeClient.h"
#include "bot/StakeTransport.h"

#include <QAction>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QSet>
#include <QUrl>
#include <functional>
#include <QActionGroup>
#include <QMoveEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QShowEvent>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QFontMetrics>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QMessageBox>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <QSplitter>
#include <QStandardPaths>
#include <QPolygonF>
#include <QStatusBar>
#include <QTableView>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace
{
// Dark theme, shared by every window (root of the app's registry key, outside the
// per-window "Windows/<slot>" groups).
constexpr char kThemeKey[] = "Theme/dark";


// qBittorrent-style light chrome, matching the reference project's palette.
const char kLightStylesheet[] = R"(
QWidget { background: #ffffff; color: #202020; font-size: 12px; }
QMainWindow, QMenuBar, QToolBar, QStatusBar { background: #eeeeee; }
QMenuBar { border-bottom: 1px solid #c8c8c8; padding: 1px 2px; }
QMenuBar::item { background: transparent; padding: 3px 8px; border-radius: 2px; }
QMenuBar::item:selected { background: #dde6f5; }
QMenu { background: #ffffff; border: 1px solid #c8c8c8; padding: 2px; }
QMenu::item { padding: 3px 24px 3px 20px; border-radius: 2px; }
QMenu::item:selected { background: #4a77c7; color: #ffffff; }
QMenu::separator { height: 1px; background: #c8c8c8; margin: 3px 6px; }
QToolTip { background: #ffffdc; border: 1px solid #c8c8c8; color: #202020; padding: 2px; }
QToolBar { border: none; spacing: 2px; padding: 1px; }
QToolBar::separator { background: #c8c8c8; width: 1px; margin: 5px 4px; }
QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 4px; background: transparent; }
QToolButton:hover { background: #dde6f5; border-color: #c8c8c8; }
QToolButton:pressed { background: #c9d9f1; }
QToolButton:disabled { color: #b4b4b4; }
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #ececec);
    border: 1px solid #bdbdbd; border-radius: 3px; padding: 3px 12px; min-height: 18px;
}
QPushButton:hover { border-color: #4a77c9; }
QPushButton:pressed { background: #e2e2e2; }
QPushButton:disabled { color: #a8a8a8; background: #f2f2f2; border-color: #d4d4d4; }
QPushButton:checked { background: #4a77c7; color: #ffffff; border-color: #3a63a8; }
QPushButton#filterBtn {
    border: none; border-radius: 0; background: transparent;
    text-align: left; padding: 4px 8px; min-height: 18px;
}
QPushButton#filterBtn:hover { background: #dde6f5; border: none; }
QPushButton#filterBtn:checked { background: #4a77c7; color: #ffffff; border: none; }
QPushButton#sectionHeader {
    background: #eeeeee; border: none; border-bottom: 1px solid #c8c8c8;
    font-weight: bold; padding: 3px 6px; text-align: left; min-height: 16px;
}
QPushButton#sectionHeader:disabled { background: #eeeeee; color: #202020; border-bottom: 1px solid #c8c8c8; }
QPushButton#rollDiceBtn { font-weight: bold; padding: 6px 12px; }
QPushButton#amountBtn { padding: 3px 0px; }
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
    background: #ffffff; border: 1px solid #bdbdbd; border-radius: 2px;
    padding: 1px 4px; min-height: 18px;
}
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: #8f8f8f; }
QComboBox QAbstractItemView { background: #ffffff; border: 1px solid #c8c8c8; selection-background-color: #4a77c7; selection-color: #ffffff; }
QComboBox QAbstractItemView { outline: 0; padding: 2px; }
QComboBox QAbstractItemView::item { padding: 3px 24px 3px 8px; min-height: 18px; border-radius: 2px; }
QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background: #4a77c7; color: #ffffff; }
QComboBox { combobox-popup: 0; }
QTableView {
    background: #ffffff; alternate-background-color: #f6f8fb; border: none;
    gridline-color: #ececec; selection-background-color: #4a77c7; selection-color: #ffffff;
}
QHeaderView::section {
    background: #eeeeee; border: none; border-right: 1px solid #dcdcdc;
    border-bottom: 1px solid #c8c8c8; padding: 3px 6px;
}
QTabWidget::pane { border: none; background: #ffffff; }
QTabBar::tab {
    background: #e4e4e4; border: 1px solid #c8c8c8; border-bottom: none;
    padding: 4px 14px; margin-right: 1px;
    border-top-left-radius: 3px; border-top-right-radius: 3px;
}
QTabBar::tab:selected { background: #ffffff; margin-bottom: -1px; }
QTabBar::tab:!selected { color: #606060; }
QTabBar::tab:!selected:hover { background: #dde6f5; }
QStatusBar { border-top: 1px solid #c8c8c8; }
QStatusBar QLabel { padding: 0 4px; background: transparent; }
QSplitter::handle { background: #d8d8d8; width: 1px; height: 1px; }
QSplitter#centralSplitter::handle:horizontal { background: transparent; width: 4px; border-left: 1px solid #d8d8d8; }
QDockWidget::title { background: #eeeeee; border-bottom: 1px solid #c8c8c8; padding: 4px; text-align: left; }
QPlainTextEdit { background: #ffffff; border: 1px solid #c8c8c8; selection-background-color: #4a77c7; selection-color: #ffffff; }
QListWidget { background: #ffffff; border: 1px solid #c8c8c8; selection-background-color: #4a77c7; selection-color: #ffffff; }
QScrollBar:vertical { background: #eeeeee; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #c4c4c4; border-radius: 5px; min-height: 20px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height: 0; background: none; }
QScrollBar:horizontal { background: #eeeeee; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: #c4c4c4; border-radius: 5px; min-width: 20px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { width: 0; background: none; }
QSlider::groove:horizontal { height: 4px; background: #e0e0e0; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; margin: -5px 0; background: #4a77c7; border-radius: 6px; }
QSlider::handle:horizontal:hover { background: #3a63a8; }
QToolBar QWidget { background: transparent; }
QToolBar QComboBox { background: #ffffff; }
QToolBar QComboBox QWidget { background: #ffffff; }
QToolBar QComboBox QAbstractItemView { background: #ffffff; color: #202020; }
QToolBar QComboBox QAbstractItemView::item:hover, QToolBar QComboBox QAbstractItemView::item:selected { background: #4a77c7; color: #ffffff; }
)";

// Dark variant of the same design.
const char kDarkStylesheet[] = R"(
QWidget { background: #2b2e33; color: #c8ccd0; font-size: 12px; }
QMainWindow, QMenuBar, QToolBar, QStatusBar { background: #2b2e33; }
QMenuBar { border-bottom: 1px solid #3a3f45; }
QMenuBar::item { background: transparent; padding: 3px 8px; border-radius: 2px; }
QMenuBar::item:selected { background: #3d4349; }
QMenu { background: #2b2e33; border: 1px solid #454a50; }
QMenu::item:selected { background: #4a77c7; color: #ffffff; }
QMenu::separator { height: 1px; background: #454a50; margin: 3px 6px; }
QToolTip { color: #c8ccd0; background-color: #32363b; border: 1px solid #454a50; }
QToolBar { border: none; spacing: 2px; }
QToolBar::separator { background: #454a50; width: 1px; margin: 5px 4px; }
QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 4px; background: transparent; }
QToolButton:hover { background: #3d4349; border-color: #454a50; }
QToolButton:disabled { color: #5a6068; }
QPushButton {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a3f45, stop:1 #31353a);
    border: 1px solid #4a5056; border-radius: 3px; padding: 3px 12px; min-height: 18px; color: #c8ccd0;
}
QPushButton:hover { border-color: #4a77c9; }
QPushButton:disabled { color: #60666d; background: #2d3136; }
QPushButton:checked { background: #4a77c7; color: #ffffff; }
QPushButton#filterBtn {
    border: none; border-radius: 0; background: transparent;
    text-align: left; padding: 4px 8px; min-height: 18px;
}
QPushButton#filterBtn:hover { background: #3d4349; border: none; }
QPushButton#filterBtn:checked { background: #4a77c7; color: #ffffff; border: none; }
QPushButton#sectionHeader {
    background: #32363b; border: none; border-bottom: 1px solid #454a50;
    font-weight: bold; padding: 3px 6px; text-align: left; min-height: 16px; color: #c8ccd0;
}
QPushButton#sectionHeader:disabled { background: #32363b; color: #c8ccd0; border-bottom: 1px solid #454a50; }
QPushButton#rollDiceBtn { font-weight: bold; padding: 6px 12px; }
QPushButton#amountBtn { padding: 3px 0px; }
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
    background: #232629; border: 1px solid #4a5056; border-radius: 2px;
    padding: 1px 4px; min-height: 18px; color: #c8ccd0;
}
QComboBox { combobox-popup: 0; }
QComboBox QAbstractItemView { background: #232629; border: 1px solid #454a50; selection-background-color: #4a77c7; selection-color: #ffffff; }
QComboBox QAbstractItemView { outline: 0; padding: 2px; }
QComboBox QAbstractItemView::item { padding: 3px 24px 3px 8px; min-height: 18px; border-radius: 2px; }
QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background: #4a77c7; color: #ffffff; }
QTableView {
    background: #232629; alternate-background-color: #292d31; border: none;
    gridline-color: #33383d; selection-background-color: #4a77c7; selection-color: #ffffff;
}
QHeaderView::section {
    background: #2b2e33; border: none; border-right: 1px solid #3a3f45;
    border-bottom: 1px solid #454a50; padding: 3px 6px;
}
QTabWidget::pane { border: none; background: #2b2e33; }
QTabBar::tab {
    background: #24272b; border: 1px solid #454a50; border-bottom: none;
    padding: 4px 14px; margin-right: 1px; color: #9aa0a6;
    border-top-left-radius: 3px; border-top-right-radius: 3px;
}
QTabBar::tab:selected { background: #2b2e33; color: #c8ccd0; margin-bottom: -1px; }
QTabBar::tab:!selected:hover { background: #3d4349; }
QStatusBar { border-top: 1px solid #454a50; }
QStatusBar QLabel { padding: 0 4px; background: transparent; }
QSplitter::handle { background: #3a3f45; width: 1px; height: 1px; }
QSplitter#centralSplitter::handle:horizontal { background: transparent; width: 4px; border-left: 1px solid #3a3f45; }
QDockWidget::title { background: #2b2e33; border-bottom: 1px solid #454a50; padding: 4px; text-align: left; }
QPlainTextEdit { background: #232629; border: 1px solid #454a50; }
QListWidget { background: #232629; border: 1px solid #454a50; selection-background-color: #4a77c7; selection-color: #ffffff; }
QScrollBar:vertical { background: #2b2e33; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #4a5056; border-radius: 5px; min-height: 20px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height: 0; background: none; }
QScrollBar:horizontal { background: #2b2e33; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: #4a5056; border-radius: 5px; min-width: 20px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { width: 0; background: none; }
QSlider::groove:horizontal { height: 4px; background: #3a3f45; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; margin: -5px 0; background: #4a77c7; border-radius: 6px; }

)";

// Draws check boxes itself (border + blue fill + white tick when checked), so no
// image files or style-sheet indicator rules are needed. Works for both themes.
class CheckStyle : public QProxyStyle
{
public:
    explicit CheckStyle(const QString &key) : QProxyStyle(key) {}

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                       const QWidget *w) const override
    {
        if (pe != PE_IndicatorCheckBox) {
            QProxyStyle::drawPrimitive(pe, opt, p, w);
            return;
        }

        const bool enabled = opt->state & State_Enabled;
        const bool on = opt->state & State_On;
        const bool partial = opt->state & State_NoChange;
        const bool hover = enabled && (opt->state & State_MouseOver);
        // Both stylesheets set a text colour: light text means the dark theme.
        const bool dark = opt->palette.color(QPalette::WindowText).lightness() > 128;

        QColor border = dark ? QColor("#4a5056") : QColor("#8f8f8f");
        QColor fill = dark ? QColor("#232629") : QColor("#ffffff");
        if (on || partial) {
            fill = QColor("#4a77c7");
            border = QColor("#3a63a8");
        }
        if (hover && !(on || partial))
            border = QColor("#4a77c9");
        if (!enabled) {
            if (on || partial) {
                fill = QColor("#a9bde0");
                border = QColor("#a9bde0");
            } else {
                fill = dark ? QColor("#2d3136") : QColor("#f2f2f2");
                border = dark ? QColor("#3a3f45") : QColor("#c8c8c8");
            }
        }

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(opt->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->setPen(QPen(border, 1.0));
        p->setBrush(fill);
        p->drawRoundedRect(r, 2.0, 2.0);

        if (on || partial) {
            QPen tickPen(Qt::white, qMax(1.6, r.width() * 0.14), Qt::SolidLine,
                         Qt::RoundCap, Qt::RoundJoin);
            p->setPen(tickPen);
            p->setBrush(Qt::NoBrush);
            QPainterPath path;
            if (partial) {
                path.moveTo(r.left() + r.width() * 0.25, r.center().y());
                path.lineTo(r.left() + r.width() * 0.75, r.center().y());
            } else {
                path.moveTo(r.left() + r.width() * 0.22, r.top() + r.height() * 0.52);
                path.lineTo(r.left() + r.width() * 0.43, r.top() + r.height() * 0.72);
                path.lineTo(r.left() + r.width() * 0.78, r.top() + r.height() * 0.30);
            }
            p->drawPath(path);
        }
        p->restore();
    }
};

QString formatAmount(double value, int decimals = 8)
{
    return QString::number(value, 'f', decimals);
}

QString formatSigned(double value, int decimals = 8)
{
    return (value >= 0.0 ? QStringLiteral("+") : QString()) + formatAmount(value, decimals);
}

} // namespace

namespace
{
// Renders a short text (e.g. "#") into a toolbar icon in the same blue as the
// other toolbar icons, so it sits naturally next to them.
QIcon makeTextIcon(const QString &text, const QColor &color)
{
    QIcon icon;
    for (const int size : {24, 48}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont f(QStringLiteral("Segoe UI"));
        f.setBold(true);
        f.setPixelSize(int(size * 0.95));
        p.setFont(f);
        p.setPen(color);
        p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, text);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// Holds the bottom tab panel at the width it has when nothing is docked on the
// right. When the Lua dock is widened the holder shrinks, but the panel keeps
// its full width and is simply clipped (covered) instead of being squeezed.
// A horizontal scroll bar along the bottom edge appears whenever part of the
// panel is covered, so the hidden part can be scrolled into view.
class FixedWidthHolder : public QWidget
{
public:
    explicit FixedWidthHolder(QWidget *child, QWidget *parent = nullptr)
        : QWidget(parent), m_child(child)
    {
        m_child->setParent(this);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);

        m_hbar = new QScrollBar(Qt::Horizontal, this);
        m_hbar->setRange(0, 0);
        m_hbar->hide();
        QObject::connect(m_hbar, &QScrollBar::valueChanged, this, [this](int) { relayout(); });
        // Switching tabs can change how wide the content is (the Profit Chart
        // follows the visible width, so it never needs scrolling).
        if (auto *tabs = qobject_cast<QTabWidget *>(m_child)) {
            QObject::connect(tabs, &QTabWidget::currentChanged, this, [this](int) {
                QTimer::singleShot(0, this, [this] { relayout(); });
            });
        }
    }

    QSize sizeHint() const override
    {
        QSize s = m_child->sizeHint();
        s.rheight() += scrollBarHeight();
        return s;
    }
    QSize minimumSizeHint() const override { return QSize(0, 0); }

    // Height taken by the scroll bar right now (0 while it is hidden).
    int scrollBarHeight() const
    {
        return m_hbar->isVisibleTo(const_cast<FixedWidthHolder *>(this))
                   ? m_hbar->sizeHint().height()
                   : 0;
    }

    // Called with +height / -height when the scroll bar appears / disappears, so
    // the owner can grow / shrink the pane and the content keeps its height.
    void setScrollBarToggledCallback(std::function<void(int)> cb)
    {
        m_scrollBarToggledCb = std::move(cb);
    }

    // Called with the width that is actually visible (this widget's own width)
    // every time the holder is re-laid out. Lets a page inside the clipped child
    // follow the visible width instead of keeping the full, partly covered one.
    void setVisibleWidthCallback(std::function<void(int)> cb)
    {
        m_visibleWidthCb = std::move(cb);
        relayout();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout();
    }
    void moveEvent(QMoveEvent *event) override
    {
        QWidget::moveEvent(event);
        relayout();
    }
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        relayout();
    }
    // Wheel turns that no control inside the panel used scroll it sideways.
    void wheelEvent(QWheelEvent *event) override
    {
        if (m_hbar->isVisibleTo(this))
            QCoreApplication::sendEvent(m_hbar, event);
        else
            QWidget::wheelEvent(event);
    }

private:
    void relayout()
    {
        if (m_inRelayout)
            return;
        m_inRelayout = true;

        const int visibleW = width();
        int w = visibleW;
        // Width available if no dock took space on the right: from this widget's
        // left edge to the right edge of the main window.
        if (QWidget *top = window(); top && top != this)
            w = std::max(w, top->width() - mapTo(top, QPoint(0, 0)).x());
        // Before the child is resized, so a page limited to the visible width is
        // already capped (or released again) when the child's layout runs.
        if (m_visibleWidthCb)
            m_visibleWidthCb(visibleW);

        // How much of the content actually lies past the visible width. A page
        // capped to the visible width (Profit Chart) has nothing to scroll; any
        // other page also gets its minimum width, so a narrow window scrolls
        // instead of squeezing the controls together.
        int contentW = w;
        if (auto *tabs = qobject_cast<QTabWidget *>(m_child)) {
            if (QWidget *page = tabs->currentWidget()) {
                const int left = page->parentWidget() ? page->mapTo(m_child, QPoint(0, 0)).x() : 0;
                if (page->maximumWidth() < QWIDGETSIZE_MAX) {
                    contentW = std::min(w, std::max(visibleW, left + page->maximumWidth() + 1));
                } else {
                    w = std::max(w, left + page->minimumSizeHint().width());
                    contentW = w;
                }
            }
        }

        const int range = std::max(0, contentW - visibleW);
        const bool wasShown = m_hbar->isVisibleTo(this);
        const bool show = range > 0;
        const int barH = m_hbar->sizeHint().height();
        m_hbar->setRange(0, range);
        m_hbar->setPageStep(std::max(1, visibleW));
        m_hbar->setSingleStep(20);
        m_hbar->setVisible(show);

        const int childH = std::max(0, height() - (show ? barH : 0));
        m_child->setGeometry(-m_hbar->value(), 0, w, childH);
        if (show) {
            m_hbar->setGeometry(0, childH, visibleW, barH);
            m_hbar->raise();
        }
        m_inRelayout = false;

        if (show != wasShown) {
            updateGeometry();
            if (m_scrollBarToggledCb) {
                const int delta = show ? barH : -barH;
                QTimer::singleShot(0, this, [this, delta] {
                    if (m_scrollBarToggledCb)
                        m_scrollBarToggledCb(delta);
                });
            }
        }
    }

    QWidget *m_child;
    QScrollBar *m_hbar = nullptr;
    bool m_inRelayout = false;
    std::function<void(int)> m_visibleWidthCb;
    std::function<void(int)> m_scrollBarToggledCb;
};
} // namespace

MainWindow::MainWindow(int slot, bool freshWebSession, QWidget *parent)
    : QMainWindow(parent)
    , m_slot(slot)
{
    // Set before anything can open a WebView2 window (see ui/WebViewSession.h).
    // Slot 0 keeps WebView2's default profile; other windows have their own, kept between
    // launches, so window 2 is still logged in when it is opened again.
    if (m_slot > 0)
        setProperty(WebViewSession::kProperty, WebViewSession::forSlot(m_slot, freshWebSession));

    // Every window is deleted when closed; the app quits when the last one is closed.
    setAttribute(Qt::WA_DeleteOnClose);
    openSlots().append(m_slot);

    // Custom check box drawing. Installed once for the whole app: re-setting the
    // app style when a second window opens would re-polish every existing window.
    if (!dynamic_cast<CheckStyle *>(qApp->style()))
        qApp->setStyle(new CheckStyle(QStringLiteral("Fusion")));
    updateWindowTitle();
    setWindowIcon(QIcon(QStringLiteral(":/icons/tray.svg")));

    m_engine = new BotEngine(this);
    m_provider = new SimulatedProvider;
    m_engine->setProvider(m_provider);
    m_model = new BetHistoryModel(this);

    // Live betting plumbing (inactive until enabled in Options).
    m_cookie = new CookieTransport(this);
    m_ext = new ExtensionTransport(this);
    m_shuffle = new ShuffleTransport(this);
    m_goated = new GoatedTransport(this);
    m_wolfbet = new WolfbetTransport(this);
    m_paradice = new ParadiceTransport(this);
    m_pasino = new PasinoTransport(this);
    m_tower = new TowerTransport(this);
    m_winna = new WinnaTransport(this);
    m_duel = new DuelTransport(this);
    m_stake = new StakeClient(this);
    m_stake->setTransport(m_cookie);
    m_engine->setLiveClient(m_stake);
    m_runClock = new QTimer(this);
    m_runClock->setInterval(1000);
    connect(m_runClock, &QTimer::timeout, this, &MainWindow::updateRunTime);

    m_balanceTimer = new QTimer(this);
    m_balanceTimer->setInterval(10000);
    connect(m_balanceTimer, &QTimer::timeout, this, &MainWindow::onBalanceTimer);
    connect(m_shuffle, &ShuffleTransport::balancesUpdated, this,
            &MainWindow::onShuffleBalancesUpdated);
    connect(m_shuffle, &ShuffleTransport::balancesFailed, this,
            &MainWindow::onShuffleBackgroundError);
    connect(m_goated, &GoatedTransport::balancesUpdated, this,
            &MainWindow::onGoatedBalancesUpdated);
    connect(m_wolfbet, &WolfbetTransport::balancesUpdated, this,
            &MainWindow::onWolfbetBalancesUpdated);
    connect(m_paradice, &ParadiceTransport::balancesUpdated, this,
            &MainWindow::onParadiceBalancesUpdated);
    connect(m_pasino, &PasinoTransport::balancesUpdated, this,
            &MainWindow::onPasinoBalancesUpdated);
    connect(m_tower, &TowerTransport::balancesUpdated, this,
            &MainWindow::onTowerBalancesUpdated);
    connect(m_winna, &WinnaTransport::balancesUpdated, this,
            &MainWindow::onWinnaBalancesUpdated);
    connect(m_winna, &WinnaTransport::notice, this,
            [this](const QString &message) { onLogMessage(message, false); });
    connect(m_duel, &DuelTransport::balancesUpdated, this, &MainWindow::onDuelBalancesUpdated);
    connect(m_duel, &DuelTransport::notice, this,
            [this](const QString &message) { onLogMessage(message, true); });
    connect(m_stake, &StakeClient::backgroundBalancesReady, this,
            &MainWindow::onStakeBackgroundBalances);
    connect(m_stake, &StakeClient::balancesReady, this, &MainWindow::onBalancesReady);
    connect(m_stake, &StakeClient::balancesFailed, this, &MainWindow::onBalancesFailed);
	connect(m_stake, &StakeClient::sessionLost, this, [this](const QString &reason) {
        if (m_isLoggedIn) {
            m_isLoggedIn = false;
            updateConnectionStatus();
            onLogMessage(tr("Session connection lost: %1").arg(reason), true);
        }
    });
    connect(m_stake, &StakeClient::seedRotated, this,
            [this](const QString &clientSeed, qint64 nonce, const QString &hash,
                   const QString &nextHash) {
        m_rotatingSeed = false;
        m_actRotate->setEnabled(true);
        if (clientSeed.isEmpty() && !hash.isEmpty()) // Wolfbet: only the server seed is renewed
            onLogMessage(tr("Seed pair rotated — server seed hash: %1").arg(hash), false);
        else if (hash.isEmpty()) // defensive: no hash in the reply
            onLogMessage(tr("Seed has been reset — client seed: %1").arg(clientSeed), false);
        else
            onLogMessage(tr("Seed pair rotated — client seed: %1, nonce: %2, server seed hash: %3%4")
                             .arg(clientSeed)
                             .arg(nonce)
                             .arg(hash,
                                  nextHash.isEmpty() ? QString() : tr(", next hash: %1").arg(nextHash)),
                         false);
        m_engine->seedRotationFinished();
        if (m_afterSeedRotation) { // manual / auto bet waiting for its client seed
            const auto next = std::move(m_afterSeedRotation);
            m_afterSeedRotation = nullptr;
            next();
        }
    });
    connect(m_stake, &StakeClient::seedRotateFailed, this, [this](const QString &error) {
        m_rotatingSeed = false;
        m_actRotate->setEnabled(true);
        onLogMessage(tr("Seed rotation failed: %1").arg(error), true);
        m_engine->seedRotationFinished(false);
        if (m_afterSeedRotation) {
            m_afterSeedRotation = nullptr;
            onLogMessage(tr("Not betting: the client seed could not be set."), true);
        }
    });
    connect(m_stake, &StakeClient::vaultDeposited, this, [this](double amount, const QString &currency) {
        onLogMessage(tr("Deposited %1 %2 to vault.").arg(QString::number(amount, 'f', 8), currency), false);
    });
    connect(m_stake, &StakeClient::vaultFailed, this, [this](const QString &error) {
        onLogMessage(tr("Vault deposit failed: %1").arg(error), true);
    });
    connect(m_ext, &IStakeTransport::readyChanged, this, &MainWindow::onExtensionReadyChanged);
    // Rejected extension-port connections (tabs of other casinos / unknown origins) are not
    // logged: they repeat every few seconds and only flood the log.

    createActions();
    createMenus();
    createSidebar();
    createCentralWidget();
    createToolbar();
    createStatusBar();
    createLogDock();
    installEventFilter(this); // dock separator drags: see eventFilter()
    createLuaDock();
    setupFloatingDocks();

    // ---- Engine wiring
    connect(m_engine, &BotEngine::started, this, &MainWindow::onEngineStarted);
    connect(m_engine, &BotEngine::stopped, this, &MainWindow::onEngineStopped);
    connect(m_engine, &BotEngine::pausedChanged, this, &MainWindow::onPausedChanged);
    connect(m_engine, &BotEngine::betPlaced, this, &MainWindow::onBetPlaced);
    connect(m_engine, &BotEngine::logMessage, this, &MainWindow::onLogMessage);
    connect(m_engine, &BotEngine::scriptOutput, this, [this](const QString &msg, bool isError) {
        if (m_luaDock)
            m_luaDock->appendConsole(msg, isError);
    });
    connect(m_engine, &BotEngine::gameChanged, this, [this](Game game) { setGame(game); });
    // resetstats() / resetseed() called from a Lua / JavaScript strategy
    connect(m_engine, &BotEngine::statsReset, this, [this] {
        refreshAfterStatsReset();
        onLogMessage(tr("Session statistics reset by script"), false);
    });
    connect(m_engine, &BotEngine::seedRotationRequested, this,
            [this](const QString &clientSeed) { rotateSeed(true, clientSeed); });
    // vault(amount) called from a Lua / JavaScript strategy (live mode)
    connect(m_engine, &BotEngine::vaultRequested, this, [this](double amount) {
        m_stake->vaultDeposit(amount);
    });
    // `currency = "btc"` in a Lua / JavaScript strategy
    m_engine->setCasinoHooks(
        [this]() { return m_live.casinoName(); },
        [this](const QString &casino, QString &err) { return switchCasinoFromScript(casino, err); });
    m_engine->setCurrencyHooks(
        [this]() { return m_engine->liveMode() ? m_live.currency : m_currencyTag.toLower(); },
        [this](const QString &cur, double &balance, QString &err) {
            return switchCurrencyFromScript(cur, balance, err);
        });
    connect(m_engine, &BotEngine::scriptError, this, [this](const QString &msg) {
        if (m_luaDock)
            m_luaDock->setStatus(msg, true);
    });

    connect(m_manualPanel, &ManualBetPanel::rollRequested, this, &MainWindow::onRollOnce);
    // The Manual Bet amount is saved as soon as it changes (and again on close), so it
    // survives even when the app does not exit normally.
    connect(m_manualPanel, &ManualBetPanel::amountChanged, this, [this](double amount) {
        if (m_discardSettings)
            return; // Options -> clear data: nothing is written back
        ScopedSettings settings(settingsGroup());
        settings.setValue(QStringLiteral("Manual/betAmount"), amount);
    });
    // Multiplier / Win chance (stored as the dice target + side and the limbo target) are
    // saved per window as soon as they change, like the amount. Not before they were loaded:
    // the panel's defaults would overwrite the saved values.
    connect(m_manualPanel, &ManualBetPanel::gameChanged, this, [this] {
        if (m_discardSettings || !m_manualTargetsLoaded)
            return;
        ScopedSettings settings(settingsGroup());
        saveManualTargets(settings);
    });
    connect(m_autoPanel, &AutoBetPanel::startRequested, this, &MainWindow::onStartAuto);
    connect(m_autoPanel, &AutoBetPanel::stopRequested, this, &MainWindow::onStopAuto);
    connect(m_fairPanel, &FairPanel::seedRotated, this,
            [this](const QString &msg) { onLogMessage(msg, false); });

    // Default size for a first launch; loadSettings() replaces it with the
    // geometry from the previous session when one was saved.
    resize(1100, 600);

    loadSettings();
	applyLiveSettings(m_live);
    updateModeWidgets();
    updateSessionWidgets();
    updateStatusWidgets();

    /*onLogMessage(tr("Ready. Balance %1 %2 — press Roll Dice or start auto-bet.")
                     .arg(formatAmount(m_startBalance))
                     .arg(m_currencyTag),
                 false);*/
}

MainWindow::~MainWindow()
{
    openSlots().removeAll(m_slot);
    delete m_provider;
}

// ---------------------------------------------------------------- creation

void MainWindow::createActions()
{
    m_actRollOnce = new QAction(QIcon(QStringLiteral(":/icons/list-add.svg")), tr("Roll once"), this);
    m_actRollOnce->setToolTip(tr("Roll once (manual bet)"));
    m_actRollOnce->setShortcut(QKeySequence(Qt::Key_Return));
    connect(m_actRollOnce, &QAction::triggered, this, &MainWindow::onRollOnce);

    m_actClear = new QAction(QIcon(QStringLiteral(":/icons/list-remove.svg")), tr("Clear history"), this);
    connect(m_actClear, &QAction::triggered, this, &MainWindow::onClearHistory);

    m_actStart = new QAction(QIcon(QStringLiteral(":/icons/torrent-start.svg")), tr("Start auto-bet"), this);
    m_actStart->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStartAuto);

    m_actStop = new QAction(QIcon(QStringLiteral(":/icons/torrent-stop.svg")), tr("Stop auto-bet"), this);
    m_actStop->setShortcut(QKeySequence(Qt::Key_F8));
    m_actStop->setEnabled(false);
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStopAuto);

    m_actPause = new QAction(QIcon(QStringLiteral(":/icons/pause-session.svg")), tr("Pause auto-bet"), this);
    m_actPause->setShortcut(QKeySequence(Qt::Key_F6));
    m_actPause->setToolTip(tr("Pause auto-bet (F6)"));
    m_actPause->setEnabled(false);
    connect(m_actPause, &QAction::triggered, this, &MainWindow::onPauseAuto);

    m_actRotate = new QAction(QIcon(QStringLiteral(":/icons/security-high.svg")), tr("Rotate seed"), this);
    connect(m_actRotate, &QAction::triggered, this, &MainWindow::onRotateSeed);

    m_actStats = new QAction(QIcon(QStringLiteral(":/icons/view-statistics.svg")), tr("Statistics"), this);
    connect(m_actStats, &QAction::triggered, this, &MainWindow::onSwitchToChart);

    // Toolbar placement matters: this replaces the old "Clear history" button
    // and keeps its list-remove icon.
    m_actReset = new QAction(QIcon(QStringLiteral(":/icons/list-remove.svg")), tr("Reset session"), this);
    connect(m_actReset, &QAction::triggered, this, &MainWindow::onResetSession);

    m_actOptions = new QAction(QIcon(QStringLiteral(":/icons/configure.svg")), tr("Options"), this);
    connect(m_actOptions, &QAction::triggered, this, &MainWindow::onOpenOptions);

    // "+" toolbar button: pick any file.
    m_actOpenFile = new QAction(QIcon(QStringLiteral(":/icons/list-add.svg")), tr("New window"), this);
    m_actOpenFile->setToolTip(tr("New window"));
    connect(m_actOpenFile, &QAction::triggered, this, &MainWindow::onOpenFile);

    // Condition builder button, next to Options.
    QIcon prefIcon;
    if (QFile::exists(QStringLiteral(":/icons/preferences-desktop.svg")))
        prefIcon = QIcon(QStringLiteral(":/icons/preferences-desktop.svg"));
    else
        prefIcon = QIcon::fromTheme(QStringLiteral("preferences-desktop"));
    m_actConditions = new QAction(prefIcon, tr("Condition builder"), this);
    m_actConditions->setToolTip(tr("Condition builder"));
    connect(m_actConditions, &QAction::triggered, this, &MainWindow::onOpenConditionBuilder);

    // "#" button: shows / hides the dockable strategy editor (Lua / JavaScript).
    m_actLua = new QAction(makeTextIcon(QStringLiteral("#"), QColor("#1e90ff")),
                           tr("Strategy editor (Lua / JavaScript)"), this);
    m_actLua->setToolTip(tr("Strategy editor (Lua / JavaScript)"));
    m_actLua->setCheckable(true);
    connect(m_actLua, &QAction::toggled, this, [this](bool visible) {
        if (!m_luaDock)
            return;
        m_luaDock->setVisible(visible);
        if (visible)
            m_luaDock->raise(); // tabbed with another dock: bring this tab to the front
    });

    m_actDarkTheme = new QAction(tr("Dark theme"), this);
    m_actDarkTheme->setCheckable(true);
    connect(m_actDarkTheme, &QAction::toggled, this, &MainWindow::onToggleDarkTheme);

    m_actShowLog = new QAction(tr("Show log"), this);
    m_actShowLog->setCheckable(true);
    connect(m_actShowLog, &QAction::toggled, this, [this](bool visible) {
        if (!m_logDock)
            return;
        m_logDock->setVisible(visible);
        if (visible)
            m_logDock->raise(); // tabbed with another dock: bring this tab to the front
    });

    // Panel toggles (View menu). The panels themselves are created later, so
    // the actions look the widget up through the member pointer when toggled.
    m_actShowSidebar = new QAction(tr("Show sidebar"), this);
    m_actShowSidebar->setCheckable(true);
    m_actShowSidebar->setChecked(true);
    connect(m_actShowSidebar, &QAction::toggled, this, [this](bool visible) {
        if (!m_sidebarDock)
            return;
        m_sidebarDock->setVisible(visible);
        // When the panel is tabbed with the Lua / JS editor, showing it must bring its tab
        // to the front instead of leaving the editor on top.
        if (visible)
            m_sidebarDock->raise();
    });

    m_actShowBetList = new QAction(tr("Show bet list"), this);
    m_actShowBetList->setCheckable(true);
    m_actShowBetList->setChecked(true);
    connect(m_actShowBetList, &QAction::toggled, this, [this](bool visible) {
        if (!m_betListDock)
            return;
        m_betListDock->setVisible(visible);
        if (visible) {
            m_betListDock->raise(); // tabbed with another dock: bring this tab to the front
            QTimer::singleShot(0, this, [this] { onTabChanged(m_tabs->currentIndex()); });
        }
    });

    m_actShowTabs = new QAction(tr("Show bottom panel"), this);
    m_actShowTabs->setCheckable(true);
    m_actShowTabs->setChecked(true);
    connect(m_actShowTabs, &QAction::toggled, this, [this](bool visible) {
        if (!m_tabs)
            return;
        m_tabs->parentWidget()->setVisible(visible); // the holder around the tabs
        if (visible) // re-fit the pane to the current tab's content height
            QTimer::singleShot(0, this, [this] { onTabChanged(m_tabs->currentIndex()); });
        else // the bet list takes the panel's height
            QTimer::singleShot(0, this, &MainWindow::updateCentralHeight);
    });
}

void MainWindow::createMenus()
{
    // Casino: which site the live bets go to. Stake keeps its login methods; Shuffle uses
    // "Direct mode" (plain HTTPS + websocket, no WebView2 / browser extension).
    QMenu *casinoMenu = menuBar()->addMenu(tr("&Casino"));
    m_casinoGroup = new QActionGroup(this);
    m_casinoGroup->setExclusive(true);
    m_actCasinoStake = casinoMenu->addAction(tr("&Stake.com"));
    m_actCasinoShuffle = casinoMenu->addAction(tr("&Shuffle.com"));
    m_actCasinoGoated = casinoMenu->addAction(tr("&Goated.com"));
    m_actCasinoWolfbet = casinoMenu->addAction(tr("&Wolfbet.com"));
    m_actCasinoDuel = casinoMenu->addAction(tr("&Duel.com"));
    m_actCasinoParadice = casinoMenu->addAction(tr("&Paradice.in"));
    m_actCasinoPasino = casinoMenu->addAction(tr("Pas&ino.com"));
    m_actCasinoTower = casinoMenu->addAction(tr("&Tower.bet"));
    m_actCasinoWinna = casinoMenu->addAction(tr("Wi&nna.com"));
    for (QAction *a : {m_actCasinoStake, m_actCasinoShuffle, m_actCasinoGoated, m_actCasinoWolfbet,
                       m_actCasinoDuel, m_actCasinoParadice, m_actCasinoPasino, m_actCasinoTower,
                       m_actCasinoWinna}) {
        a->setCheckable(true);
        m_casinoGroup->addAction(a);
    }
    casinoAction(m_live.casino)->setChecked(true);
    connect(m_actCasinoStake, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Stake); });
    connect(m_actCasinoShuffle, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Shuffle); });
    connect(m_actCasinoGoated, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Goated); });
    connect(m_actCasinoWolfbet, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Wolfbet); });
    connect(m_actCasinoDuel, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Duel); });
    connect(m_actCasinoParadice, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Paradice); });
    connect(m_actCasinoPasino, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Pasino); });
    connect(m_actCasinoTower, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Tower); });
    connect(m_actCasinoWinna, &QAction::triggered, this,
            [this] { onCasinoSelected(LiveSettings::Casino::Winna); });

    QMenu *gameMenu = menuBar()->addMenu(tr("&Game"));
    m_gameGroup = new QActionGroup(this);
    m_gameGroup->setExclusive(true);
    m_actGameDice = gameMenu->addAction(tr("&Dice"));
    m_actGameLimbo = gameMenu->addAction(tr("&Limbo"));
    for (QAction *a : {m_actGameDice, m_actGameLimbo}) {
        a->setCheckable(true);
        m_gameGroup->addAction(a);
    }
    m_actGameDice->setChecked(true);
    connect(m_actGameDice, &QAction::triggered, this, [this] { onGameSelected(Game::Dice); });
    connect(m_actGameLimbo, &QAction::triggered, this, [this] { onGameSelected(Game::Limbo); });

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_actShowSidebar);
    viewMenu->addAction(m_actShowBetList);
    viewMenu->addAction(m_actShowTabs);
    viewMenu->addAction(m_actShowLog);
    viewMenu->addAction(m_actLua);
    viewMenu->addSeparator();
    viewMenu->addAction(m_actDarkTheme);

    QMenu *botMenu = menuBar()->addMenu(tr("&Bot"));
    botMenu->addAction(m_actStart);
    botMenu->addAction(m_actPause);
    botMenu->addAction(m_actStop);
    botMenu->addSeparator();
    botMenu->addAction(m_actRollOnce);
    botMenu->addAction(m_actReset);
    botMenu->addAction(m_actClear);
    botMenu->addSeparator();
    botMenu->addAction(m_actRotate);

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(m_actStats);
    toolsMenu->addAction(m_actOptions);
    toolsMenu->addAction(m_actConditions);

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("E&xit"), qApp, &QApplication::closeAllWindows);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::onAbout);
}

void MainWindow::createToolbar()
{
    auto *toolbar = addToolBar(QString());
    toolbar->setObjectName(QStringLiteral("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    toolbar->addAction(m_actOpenFile);
    toolbar->addAction(m_actReset);
    toolbar->addSeparator();
    toolbar->addAction(m_actStart);
    toolbar->addAction(m_actPause);
    toolbar->addAction(m_actStop);
    toolbar->addSeparator();
    toolbar->addAction(m_actRotate);
    toolbar->addAction(m_actStats);
    toolbar->addAction(m_actOptions);
    toolbar->addAction(m_actConditions);
    toolbar->addAction(m_actLua);
    toolbar->addSeparator();

    // Filter box on the right, like the reference toolbar.
    auto *filterBox = new QWidget;
    auto *filterLayout = new QHBoxLayout(filterBox);
    filterLayout->setContentsMargins(0, 0, 2, 0);
    filterLayout->setSpacing(2);
	
    auto *findIcon = new QLabel;
    findIcon->setPixmap(QIcon(QStringLiteral(":/icons/edit-find.svg")).pixmap(14, 14));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Filter by nonce or roll..."));
    m_searchEdit->setFixedWidth(220);
    m_searchEdit->setClearButtonEnabled(true);
	
	m_searchEdit->addAction(QIcon(QStringLiteral(":/icons/edit-find.svg")),
                            QLineEdit::LeadingPosition);

	
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchEdited);
    //filterLayout->addWidget(findIcon);
    filterLayout->addWidget(m_searchEdit);
    toolbar->addWidget(filterBox);
}

void MainWindow::createSidebar()
{
    // The sidebar content keeps its natural height. The scroll area around it
    // only clips, so when the Log dock shrinks the central area the SESSION
    // rows are covered from the bottom instead of being squeezed together.
    auto *sidebarContent = new QWidget;
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(sidebarContent);
    scroll->setMinimumHeight(0);
    scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    m_sidebar = scroll;
    // Resizable: drag the splitter handle on its right edge (the width is saved with
    // MainWindow/centralSplitter).
    m_sidebar->setMinimumWidth(150);
    m_sidebar->setMaximumWidth(700);

    // The STATUS / SESSION panel is a dock widget, like the Log: it can be dragged to any
    // side of the window, floated, or closed (View > Show sidebar brings it back).
    m_sidebarDock = new QDockWidget(tr("Status / Session"), this);
    m_sidebarDock->setObjectName(QStringLiteral("sidebarDock"));
    m_sidebarDock->setWidget(m_sidebar);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
    resizeDocks({m_sidebarDock}, {210}, Qt::Horizontal);
    connect(m_sidebarDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // Keep the View menu check in sync when the dock is closed via its X.
        if (m_actShowSidebar && m_actShowSidebar->isChecked() != visible) {
            const QSignalBlocker blocker(m_actShowSidebar);
            m_actShowSidebar->setChecked(visible);
        }
    });
    // The session values shrink their font to the panel's width: re-fit when it is resized
    // (dock splitter dragged, panel floated, moved to another side ...).
    m_sidebar->installEventFilter(this);
    auto *layout = new QVBoxLayout(sidebarContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto makeHeader = [](const QString &text) {
        auto *label = new QPushButton(text);
        label->setObjectName(QStringLiteral("sectionHeader"));
        label->setEnabled(false);
        return label;
    };

    layout->addWidget(makeHeader(tr("STATUS")));

    const QList<QPair<QString, QString>> filterDefs = {
        {tr("All"), QStringLiteral(":/icons/filter-all.svg")},
        {tr("Wins"), QStringLiteral(":/icons/checked-completed.svg")},
        {tr("Losses"), QStringLiteral(":/icons/error.svg")},
        {tr("Big wins (10x+)"), QStringLiteral(":/icons/filter-active.svg")},
    };

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int row = 0; row < filterDefs.size(); ++row) {
        auto *btn = new QPushButton(QStringLiteral("%1 (0)").arg(filterDefs.at(row).first));
        btn->setCheckable(true);
        btn->setIcon(QIcon(filterDefs.at(row).second));
        btn->setIconSize(QSize(16, 16));
        btn->setFlat(true);
        btn->setObjectName(QStringLiteral("filterBtn"));
        btn->setProperty("filterRow", row);
        btn->setProperty("baseLabel", filterDefs.at(row).first);
        btn->setToolTip(tr("Filter the bet list"));
        if (row == 0)
            btn->setChecked(true);
        group->addButton(btn, row);
        layout->addWidget(btn);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        m_model->setFilter(static_cast<BetHistoryModel::Filter>(id));
    });

    // The reference app puts a small gap between the filter list and SESSION.
    layout->addSpacing(6);
    layout->addWidget(makeHeader(tr("SESSION")));

    auto *sessionGrid = new QGridLayout;
    sessionGrid->setContentsMargins(8, 6, 8, 6);
    sessionGrid->setHorizontalSpacing(8);
    sessionGrid->setVerticalSpacing(4);
    // Same font family as the statistics text in the web UI (index.js): Montserrat, in bold.
    // Falls back to Segoe UI / Arial when Montserrat is not installed.
    QFont mono(QStringLiteral("Montserrat"));
    mono.setFamilies({QStringLiteral("Montserrat"), QStringLiteral("Segoe UI"), QStringLiteral("Arial")});
    mono.setStyleHint(QFont::SansSerif);
    mono.setPointSizeF(8.5);
    mono.setBold(true); // session values in bold
    auto makeSessionRow = [&](const QString &name, QLabel **value, int r) {
        auto *nameLabel = new QLabel(name);
        nameLabel->setStyleSheet(QStringLiteral("color:#737373;"));
        *value = new QLabel(QStringLiteral("—"));
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*value)->setFont(mono);
        // Label and value share one cell. The label sits on the left; the value is only
        // as wide as its own text and is right-aligned, so a short value leaves the label
        // visible, while a long value grows to the left and covers part of the label
        // (it is created after the label, so it is drawn on top and has an opaque
        // background).
        (*value)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        (*value)->setAutoFillBackground(true);
        auto *rowGrid = new QGridLayout;
        rowGrid->setContentsMargins(0, 0, 0, 0);
        rowGrid->addWidget(nameLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
        rowGrid->addWidget(*value, 0, 0, Qt::AlignRight | Qt::AlignVCenter);
        sessionGrid->addLayout(rowGrid, r, 0, 1, 2);
    };
    makeSessionRow(tr("Balance"), &m_lblBalance, 0);
    makeSessionRow(tr("Profit"), &m_lblProfit, 1);
    makeSessionRow(tr("Wager"), &m_lblWagered, 2);
    makeSessionRow(tr("Win rate"), &m_lblWinRate, 3);
    makeSessionRow(tr("Highest bet"), &m_lblHighestBet, 4);
    makeSessionRow(tr("H profit"), &m_lblHighestProfit, 5);
    makeSessionRow(tr("L profit"), &m_lblLowestProfit, 6);
    makeSessionRow(tr("Highest streak"), &m_lblHighestStreak, 7);
    makeSessionRow(tr("Lowest streak"), &m_lblLowestStreak, 8);
    makeSessionRow(tr("Current streak"), &m_lblCurrentStreak, 9);
    makeSessionRow(tr("Running time"), &m_lblRunTime, 10);
    m_lblRunTime->setText(QStringLiteral("00:00:00"));
    sessionGrid->setColumnStretch(1, 1);
    layout->addLayout(sessionGrid);
    layout->addStretch();
}

void MainWindow::createCentralWidget()
{
    // ---- Bet table ------------------------------------------------------
    m_betTable = new QTableView;
    m_betTable->setModel(m_model);
    m_betTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_betTable->setSelectionMode(QAbstractItemView::SingleSelection);
    // Zebra stripes come from the model (per bet), not from the row position.
    m_betTable->setAlternatingRowColors(false);
    m_betTable->verticalHeader()->setVisible(false);
    m_betTable->verticalHeader()->setDefaultSectionSize(20);
    m_betTable->horizontalHeader()->setStretchLastSection(false);
    m_betTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_betTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_betTable->setShowGrid(false);
    m_betTable->setSortingEnabled(false);

    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(8.5);
    m_betTable->setFont(mono);
    m_betTable->horizontalHeader()->setFont(QFont(QStringLiteral("Segoe UI"), 8));

    // Bet ID column: left-click "View" resolves the iid, right-click on any row
    // opens the copy / open-in-browser menu (same behaviour as the C# bot).
    m_betTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_betTable, &QTableView::clicked, this, &MainWindow::onBetTableClicked);
    connect(m_betTable, &QWidget::customContextMenuRequested, this,
            &MainWindow::onBetTableContextMenu);

    // Game, #, Nonce, Time, Amount, Target, Roll, Multiplier, Payout, Profit, Bet ID, Casino
    const int widths[] = {56, 44, 64, 76, 90, 76, 64, 80, 90, 96, 64, 72};
    for (int c = 0; c < BetHistoryModel::ColumnCount; ++c)
        m_betTable->setColumnWidth(c, widths[c]);

    // Empty-state hint shown when the bet list has no rows (like the reference).
    m_emptyLabel = new QLabel(
        tr("No bets yet — press the + button or Roll Dice below."), m_betTable);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color:#737373;"));
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_betTable->viewport()->installEventFilter(this);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::updateEmptyState);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateEmptyState);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateEmptyState);
    updateEmptyState();

    // ---- Auto-scroll control --------------------------------------------
    // Newest bets are inserted at row 0. While the user is following the top,
    // the view stays at the top. As soon as they scroll or click a row, the
    // view is held in place (scroll value is shifted by the inserted rows so
    // the same bets stay on screen). Scrolling back to the top resumes it.
    QScrollBar *vsb = m_betTable->verticalScrollBar();
    vsb->installEventFilter(this);
    connect(vsb, &QScrollBar::actionTriggered, this, [this](int) {
        // actionTriggered fires only for user-driven scrolling (drag, arrows, page).
        m_betFollow = false;
        QTimer::singleShot(0, this, [this] {
            if (m_betTable->verticalScrollBar()->value() == 0)
                m_betFollow = true;
        });
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] { m_betFollow = true; });

    // When the highlighted bet is pushed out of the list (beyond kMaxRows), just
    // drop the highlight. Otherwise Qt moves the selection/current row to the
    // neighbouring row and scrolls the view down to it.
    connect(m_model, &BetHistoryModel::rowAboutToBeDropped, this, [this](int row) {
        QItemSelectionModel *sm = m_betTable->selectionModel();
        if (sm->isRowSelected(row, {}) || sm->currentIndex().row() == row)
            sm->clear(); // clears selection and current index
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int first, int last) {
        if (first != 0 || m_betFollow)
            return;
        QScrollBar *sb = m_betTable->verticalScrollBar();
        if (sb->isSliderDown())
            return; // the user is dragging the thumb: don't fight them
        const int count = last - first + 1;
        m_betTable->doItemsLayout();            // refresh the scrollbar range first
        sb->setValue(sb->value() + count);      // keep the same rows on screen
    });

    // ---- Bottom panel with folder-style tabs ----------------------------
    m_tabs = new QTabWidget;
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->setDocumentMode(true);
	
	m_tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
	
    m_manualPanel = new ManualBetPanel;
    m_autoPanel = new AutoBetPanel;

    auto *chartTab = new QWidget;
    auto *chartLayout = new QVBoxLayout(chartTab);
    chartLayout->setContentsMargins(12, 12, 12, 12);
    chartLayout->setSpacing(6);
    auto *chartHeader = new QHBoxLayout;
    QFont monoBold = mono;
    monoBold.setBold(true);
    m_chartMax = new QLabel(tr("Max: —"));
    m_chartMin = new QLabel(tr("Min: —"));
    m_chartNow = new QLabel(tr("Now: —"));
    for (QLabel *label : {m_chartMax, m_chartMin, m_chartNow}) {
        label->setFont(monoBold);
        chartHeader->addWidget(label);
    }
    chartHeader->addStretch();
    chartLayout->addLayout(chartHeader);
    m_graph = new BalanceGraphWidget;
    chartLayout->addWidget(m_graph);

    m_fairPanel = new FairPanel(m_provider, settingsGroup());

    m_tabs->addTab(m_manualPanel, QIcon(QStringLiteral(":/icons/list-add.svg")), tr("Manual Bet"));
    m_tabs->addTab(m_autoPanel, QIcon(QStringLiteral(":/icons/torrent-start-forced.svg")), tr("Auto Bet"));
    m_tabs->addTab(chartTab, QIcon(QStringLiteral(":/icons/view-statistics.svg")), tr("Profit Chart"));
    m_tabs->addTab(m_fairPanel, QIcon(QStringLiteral(":/icons/security-high.svg")), tr("Provably Fair"));
    for (int i = 0; i < m_tabs->count(); ++i)
        m_tabs->widget(i)->installEventFilter(this);

    // Latest log line at the right end of the tab strip.
    m_logLineLabel = new QLabel(tr("Ready"));
    m_logLineLabel->setStyleSheet(QStringLiteral("color:#737373; padding-right: 8px;"));
    m_tabs->setCornerWidget(m_logLineLabel, Qt::BottomRightCorner);

    // Expand the bottom panel to full content height when a form-heavy tab
    // (Provably Fair / Auto Bet) becomes current, so nothing is clipped.
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    // Re-clicking the already-selected tab re-fits the panel too.
    connect(m_tabs, &QTabWidget::tabBarClicked, this, &MainWindow::onTabChanged);
    // Remember the open tab right away too (not only on close).
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_discardSettings || index < 0)
            return;
        ScopedSettings settings(settingsGroup());
        settings.setValue(QStringLiteral("MainWindow/bottomTab"), index);
    });

    // The bet list is a dock widget, like the Log and the Status / Session panel: it can be
    // dragged to any side of the window, floated, or closed (View > Show bet list brings it
    // back). By default it sits above the bottom panel (Manual / Auto / Chart / Fair), which
    // is the window's central area.
    m_betListDock = new QDockWidget(tr("Bet List"), this);
    m_betListDock->setObjectName(QStringLiteral("betListDock"));
    m_betListDock->setWidget(m_betTable);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea); // the sidebar keeps the full height
    addDockWidget(Qt::TopDockWidgetArea, m_betListDock);
    connect(m_betListDock, &QDockWidget::visibilityChanged, this, [this](bool) {
        // Keep the View menu check in sync when the dock is closed via its X. (isHidden, not
        // the signal's value: a dock tabbed behind another one is still "shown".)
        const bool shown = !m_betListDock->isHidden();
        if (m_actShowBetList && m_actShowBetList->isChecked() != shown) {
            const QSignalBlocker blocker(m_actShowBetList);
            m_actShowBetList->setChecked(shown);
        }
        QTimer::singleShot(0, this, &MainWindow::updateCentralHeight);
    });
    // Moved back above / below the panel (or out of it): re-fit the bottom panel.
    const auto refit = [this] {
        QTimer::singleShot(0, this, [this] {
            if (m_tabs)
                onTabChanged(m_tabs->currentIndex());
        });
    };
    connect(m_betListDock, &QDockWidget::dockLocationChanged, this, refit);
    connect(m_betListDock, &QDockWidget::topLevelChanged, this, refit);
    // Popping the bet list out frees its height. That must stay as empty space above the
    // bottom panel, not go to the Log: lift the panel's cap right away (before the window
    // re-lays out) and put the Log back to the height it had.
    connect(m_betListDock, &QDockWidget::topLevelChanged, this, [this](bool) {
        updateCentralHeight(); // (un)cap the central area right away, before the re-layout
        scheduleKeepLogHeight();
    });
    connect(m_betListDock, &QDockWidget::dockLocationChanged, this,
            [this] { scheduleKeepLogHeight(); });
    // The tabs live in a holder so they keep their width (and are clipped) when
    // the Lua dock is widened over them, rather than being resized.
    auto *tabsHolder = new FixedWidthHolder(m_tabs);
    // The Profit Chart is the exception: instead of being covered, it follows the
    // visible width, so the whole graph stays in view while the editor dock is resized.
    tabsHolder->setVisibleWidthCallback([this, chartTab](int visibleW) {
        const int left = chartTab->parentWidget()
                             ? chartTab->mapTo(m_tabs, QPoint(0, 0)).x()
                             : 0;
        chartTab->setMaximumWidth(std::max(1, visibleW - left - 1));
    });
    // The horizontal scroll bar under the panel takes its own row: grow the pane by
    // that much when it appears (and give it back when it goes), so the tab page
    // keeps the height it had and nothing at its bottom is covered.
    tabsHolder->setScrollBarToggledCallback([this](int) {
        QTimer::singleShot(0, this, &MainWindow::updateCentralHeight);
    });

    // No layout: the panel is placed by hand at the bottom of this area, at its content
    // height (see positionBottomPanel()), so it always sits right on the Log. Any spare height
    // stays empty above it.
    auto *right = new QWidget;
    m_centralArea = right;
    tabsHolder->setParent(right);
    right->installEventFilter(this); // Resize: positionBottomPanel()

    m_centralSplitter = new QSplitter(Qt::Horizontal);
    m_centralSplitter->setObjectName(QStringLiteral("centralSplitter"));
    m_centralSplitter->setChildrenCollapsible(false);
    m_centralSplitter->addWidget(right); // the sidebar now lives in its own dock
    // Re-fit the session values once the window is shown with its restored sidebar width.
    QTimer::singleShot(0, this, [this] { updateSessionWidgets(); });
    // Ignored = the central area reports no minimum size, so a docked panel (the
    // Lua editor) can be widened over the bet list / bottom tabs and covers them
    // instead of being stopped by their minimum widths.
    m_centralSplitter->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setCentralWidget(m_centralSplitter);
	m_centralSplitter->setMinimumHeight(1);
}

void MainWindow::createStatusBar()
{
    //m_statusStateIcon = new QLabel;
    //m_statusStateIcon->setPixmap(QIcon(QStringLiteral(":/icons/filter-inactive.svg")).pixmap(14, 14));
    //m_statusStateText = new QLabel(tr("Idle"));

	m_statusStateIcon = new QLabel;
    m_statusStateIcon->setFixedSize(12, 12);
	m_statusStateText = new QLabel(tr("Disconnected"));
	
    m_statusBets = new QLabel(tr("Bets: 0"));
    m_statusWagered = new QLabel(tr("Wagered: 0.00"));
    m_statusPl = new QLabel(tr("P/L: +0.0000"));
    m_statusSpeed = new QLabel;

    auto *balanceIcon = new QLabel;
    balanceIcon->setPixmap(QIcon(QStringLiteral(":/icons/network-server.svg")).pixmap(14, 14));
    m_statusBalance = new QLabel;
    QFont bold = m_statusBalance->font();
    bold.setBold(true);
    m_statusBalance->setFont(bold);

    statusBar()->addWidget(m_statusStateIcon);
    statusBar()->addWidget(m_statusStateText);
    statusBar()->addPermanentWidget(m_statusBets);
    statusBar()->addPermanentWidget(m_statusWagered);
    statusBar()->addPermanentWidget(m_statusPl);
    statusBar()->addPermanentWidget(m_statusSpeed);
    m_statusMode = new QLabel;
    statusBar()->addPermanentWidget(m_statusMode);
    statusBar()->addPermanentWidget(balanceIcon);
    statusBar()->addPermanentWidget(m_statusBalance);
}

void MainWindow::createLogDock()
{
    m_logWidget = new LogWidget;
    m_logDock = new QDockWidget(tr("Log"), this);
    m_logDock->setObjectName(QStringLiteral("logDock"));
    m_logDock->setWidget(m_logWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    
    // ADD THIS BLOCK:
    connect(m_logDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // Keep the View menu check in sync when the dock is closed via its X.
        if (m_actShowLog && m_actShowLog->isChecked() != visible) {
            const QSignalBlocker blocker(m_actShowLog);
            m_actShowLog->setChecked(visible);
        }
        if (visible && m_logWidget) {
            // Use a 0ms timer so the layout finishes resizing before we scroll
            QTimer::singleShot(0, m_logWidget, &QTableWidget::scrollToBottom);
        }
    });
    resizeDocks({m_logDock}, {100}, Qt::Vertical);
    m_logDock->hide();
    // The Log's height only changes when the user changes it: moved to another place or shown
    // again -> take its new height as the one to keep (see keepLogHeight()).
    connect(m_logDock, &QDockWidget::dockLocationChanged, this, [this] {
        QTimer::singleShot(100, this, &MainWindow::rememberLogHeight);
    });
    connect(m_logDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible)
            QTimer::singleShot(100, this, &MainWindow::rememberLogHeight);
    });
}

void MainWindow::createLuaDock()
{
    // The right dock area owns the corners: the script editor runs the full window
    // height, above the bottom (Log) dock instead of being cut short by it.
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    m_luaDock = new LuaEditorDock(this);
    m_luaDock->setSettingsGroup(settingsGroup());
    addDockWidget(Qt::RightDockWidgetArea, m_luaDock);
    resizeDocks({m_luaDock}, {300}, Qt::Horizontal);
    m_luaDock->hide();

    connect(m_luaDock, &LuaEditorDock::runRequested, this, &MainWindow::onRunScript);
    connect(m_luaDock, &LuaEditorDock::stopRequested, this, &MainWindow::onStopAuto);

    // Keep the "#" button in sync when the dock is closed with its X button.
    connect(m_luaDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_actLua && m_actLua->isChecked() != visible) {
            const QSignalBlocker blocker(m_actLua);
            m_actLua->setChecked(visible);
        }
    });
}

// ---------------------------------------------------------------- actions

void MainWindow::updateWindowTitle()
{
    // The main window keeps the plain title; "+" windows show their number:
    // #2 = first "+" window, #3 = the next, … (settings slot + 1).
    if (m_slot == 0)
        setWindowTitle(tr("xBot - %1").arg(casinoDisplayName(m_live.casino)));
    else
        setWindowTitle(tr("xBot #%1 - %2").arg(m_slot + 1).arg(casinoDisplayName(m_live.casino)));
}

void MainWindow::setGame(Game game)
{
    m_game = game;
    {
        const QSignalBlocker blocker(m_gameGroup);
        (game == Game::Limbo ? m_actGameLimbo : m_actGameDice)->setChecked(true);
    }
    if (m_manualPanel)
        m_manualPanel->setGame(game);
    if (m_fairPanel)
        m_fairPanel->setGame(game); // Limbo: verify shows the multiplier
    updateWindowTitle();
}

void MainWindow::onGameSelected(Game game)
{
    if (game == m_game)
        return;
    setGame(game); // the Manual Bet tab now shows (and returns) the new game's settings
    if (m_engine->isRunning() || m_engine->startPending()) {
        // Switch the running auto-bet too: the next bet is placed in the new game
        // (a bet already in flight finishes in the old one), still at the Auto Bet tab's multiplier / win chance (scripts ignore these values).
        BotConfig betCfg = m_manualPanel->config();
        m_autoPanel->applyOddsTo(betCfg);
        m_engine->switchGame(betCfg);
    }
}

QString MainWindow::casinoDisplayName(LiveSettings::Casino casino)
{
    switch (casino) {
    case LiveSettings::Casino::Shuffle: return tr("Shuffle");
    case LiveSettings::Casino::Goated:  return tr("Goated");
    case LiveSettings::Casino::Wolfbet: return tr("Wolfbet");
    case LiveSettings::Casino::Duel:    return tr("Duel");
    case LiveSettings::Casino::Paradice: return tr("Paradice");
    case LiveSettings::Casino::Pasino:  return tr("Pasino");
    case LiveSettings::Casino::Tower:   return tr("Tower");
    case LiveSettings::Casino::Winna:   return tr("Winna");
    default:                            return tr("Stake");
    }
}

QAction *MainWindow::casinoAction(LiveSettings::Casino casino) const
{
    switch (casino) {
    case LiveSettings::Casino::Shuffle: return m_actCasinoShuffle;
    case LiveSettings::Casino::Goated:  return m_actCasinoGoated;
    case LiveSettings::Casino::Wolfbet: return m_actCasinoWolfbet;
    case LiveSettings::Casino::Duel:    return m_actCasinoDuel;
    case LiveSettings::Casino::Paradice: return m_actCasinoParadice;
    case LiveSettings::Casino::Pasino:  return m_actCasinoPasino;
    case LiveSettings::Casino::Tower:   return m_actCasinoTower;
    case LiveSettings::Casino::Winna:   return m_actCasinoWinna;
    default:                            return m_actCasinoStake;
    }
}

// The Provably Fair tab is only available while Stake is the selected casino.
void MainWindow::updateFairTab()
{
    if (!m_tabs || !m_fairPanel)
        return;
    const int fairIdx = m_tabs->indexOf(m_fairPanel);
    if (fairIdx < 0)
        return;
    const bool enabled = m_live.casino == LiveSettings::Casino::Stake;
    m_tabs->setTabEnabled(fairIdx, enabled);
    if (!enabled && m_tabs->currentIndex() == fairIdx)
        m_tabs->setCurrentIndex(0); // leave the disabled tab: back to Manual Bet
}

void MainWindow::updateCasinoMenu()
{
    updateFairTab();
    if (!m_actCasinoStake || !m_actCasinoShuffle || !m_actCasinoGoated || !m_actCasinoWolfbet
        || !m_actCasinoDuel || !m_actCasinoParadice || !m_actCasinoPasino
        || !m_actCasinoTower || !m_actCasinoWinna)
        return;
    const QSignalBlocker blocker(m_casinoGroup);
    casinoAction(m_live.casino)->setChecked(true);
}

void MainWindow::onCasinoSelected(LiveSettings::Casino casino)
{
    if (m_live.casino == casino)
        return;
    if (m_engine->isRunning()) {
        // Switch the running auto-bet (also script runs) like a script's casino = "...":
        // the session is kept and the next bet goes to the new casino (after a bet in flight).
        // The menu shows the new casino once the switch has actually happened.
        updateCasinoMenu();
        m_engine->switchCasino(LiveSettings::nameOf(casino));
        return;
    }
    if (m_engine->betInFlight()) {
        updateCasinoMenu(); // undo the click: a manual bet is still on its way
        onLogMessage(tr("Wait for the bet in flight before changing the casino."), true);
        return;
    }

    // Each casino remembers its own bet currency.
    m_currencyByCasino[int(m_live.casino)] = m_live.currency;

    LiveSettings next = m_live;
    next.casino = casino;
    next.currency = m_currencyByCasino[int(casino)];

    // The session (history, balance graph, stats) is kept when the casino changes.
    applyLiveSettings(next);
    updateCasinoMenu();
    onLogMessage(tr("Casino: %1 — open Tools → Options to enter its credentials and log in.")
                     .arg(casinoDisplayName(casino)),
                 false);
}

void MainWindow::closeHiddenBrowser()
{
    if (m_webTransport)
        m_webTransport->setBrowser(nullptr);
    if (m_hiddenBrowser) {
        m_hiddenBrowser->close();
        delete m_hiddenBrowser;
        m_hiddenBrowser = nullptr;
    }
}

void MainWindow::onRollOnce()
{
    if (m_engine->isRunning())
        return;
    const auto roll = [this] {
        // Manual bets use only the Manual Bet tab (amount, multiplier / win chance, side).
        m_engine->rollOnce(manualConfig());
    };
    // The client seed is set once before the first manual bet (not before every roll).
    if (m_clientSeedPending)
        applyClientSeed(roll);
    else
        roll();
}

void MainWindow::applyClientSeed(const std::function<void()> &then)
{
    const QString typed = m_clientSeed.trimmed();
    if (!m_randomClientSeed && typed.isEmpty()) {
        m_clientSeedPending = false;
        then(); // no client seed configured: leave the seed pair alone
        return;
    }
    if (m_rotatingSeed) {
        onLogMessage(tr("A seed rotation is already under way - try again in a moment."), true);
        return;
    }
    const QString seed = m_randomClientSeed ? StakeClient::randomClientSeed(10) : typed;

    if (m_engine->liveMode() && m_live.isParadice()) {
        // Paradice's client seed cannot be set (updateServerSeed only renews the server seed):
        // do not rotate before every bet, just bet on with the current seed pair.
        m_clientSeedPending = false;
        onLogMessage(tr("Paradice: the client seed cannot be set - betting with the current "
                        "seed pair (use Rotate seed to renew the server seed)."), false);
        then();
        return;
    }
    if (m_engine->liveMode() && m_live.isTower()) {
        // Tower's client seed is not changed from the app (no seed endpoint is used yet):
        // do not rotate before every bet, just bet on with the current seed pair.
        m_clientSeedPending = false;
        onLogMessage(tr("%1: the client seed cannot be set from the app - betting with the "
                        "current seed pair (change it on the site if needed).")
                         .arg(casinoDisplayName(m_live.casino)), false);
        then();
        return;
    }
    if (m_engine->liveMode() && m_live.isWolfbet()) {
        // Wolfbet's client seed cannot be set (the refresh call only renews the server seed):
        // do not rotate before every bet, just bet on with the current seed pair.
        m_clientSeedPending = false;
        onLogMessage(tr("Wolfbet: the client seed cannot be set - betting with the current "
                        "seed pair (use Rotate seed to renew the server seed)."), false);
        then();
        return;
    }
    if (m_engine->liveMode()) {
        if (m_engine->betInFlight()) {
            onLogMessage(tr("Wait for the current bet to finish."), true);
            return;
        }
        m_clientSeedPending = false;
        m_rotatingSeed = true;
        m_actRotate->setEnabled(false);
        m_afterSeedRotation = then;
        onLogMessage(tr("Setting client seed %1…").arg(seed), false);
        m_stake->rotateSeedPair(seed); // answer: seedRotated / seedRotateFailed
        return;
    }

    m_clientSeedPending = false;
    m_fairPanel->onRotate();
    m_provider->setClientSeed(seed);
    m_fairPanel->refresh();
    onLogMessage(tr("Client seed set: %1").arg(seed), false);
    then();
}

void MainWindow::onStartAuto()
{
    if (m_engine->isRunning())
        return;
    // The configured client seed (Options -> Game) is set first; strategies started
    // with the script editor's Run button never go through here.
    applyClientSeed([this] {
        // "Use Condition Builder" ticked: always condition mode, even with no blocks. Of the
        // Auto Bet tab the base bet (first bet and "reset amount") and the multiplier / win
        // chance are used; bet count, on win / on loss and stop on profit / loss are not.
        // Only the dice side comes from the Manual tab (the Auto Bet tab has no side).
        const bool condMode = m_autoPanel->useConditions();
        BotConfig cfg;
        if (condMode) {
            cfg = m_manualPanel->config();
            cfg.game = m_game;
            cfg.startBalance = m_startBalance;
            BotConfig autoCfg;
            m_autoPanel->applyTo(autoCfg);
            cfg.baseBet = autoCfg.baseBet;
            m_autoPanel->applyOddsTo(cfg);
        } else {
            cfg = currentConfig();
        }

        m_engine->setConfig(cfg);
        m_engine->setConditionMode(condMode);
        m_engine->setConditions(condMode ? m_conditions : BlockList());
        m_engine->setScript(QString()); // the normal Start button never runs a script
        m_luaRunning = false;
        m_engine->start();
    });
}

// Run button of the strategy editor: same as Start, but the Lua / JavaScript script
// drives the bets.
void MainWindow::onRunScript(const QString &script, ScriptLanguage language)
{
    const QString lang = scriptLanguageName(language);
    if (m_engine->isRunning()) {
        onLogMessage(tr("Stop the running auto-bet before starting a %1 strategy.").arg(lang), true);
        return;
    }
    if (script.trimmed().isEmpty()) {
        onLogMessage(tr("The %1 script is empty.").arg(lang), true);
        return;
    }
    // Script mode ignores the Manual / Auto Bet tabs and the condition blocks.
    BotConfig luaCfg;
    luaCfg.startBalance = m_startBalance;
    luaCfg.game = m_game; // the script can override it with game = "dice" / "limbo"
    m_engine->setConfig(luaCfg);
    m_engine->setConditions(BlockList());
    m_engine->setConditionMode(false);
    m_engine->setScript(script, language);
    //m_luaDock->appendConsole(tr("— run started —"), false);
    m_luaRunning = true;
    m_engine->start();
    m_luaRunning = m_engine->isRunning() || m_engine->startPending();
    m_luaDock->setRunning(m_luaRunning);
}

void MainWindow::onStopAuto()
{
    m_engine->stop();
}

void MainWindow::onPauseAuto()
{
    if (!m_engine->isRunning())
        return;
    if (m_engine->isPaused())
        m_engine->resume();
    else
        m_engine->pause();
}

void MainWindow::updateConnectionStatus()
{
    bool connected = false;
    
    // Must be live enabled, successfully logged in, and transport must be active
    if (m_live.enabled && m_engine->liveMode() && m_isLoggedIn) {
        QString why;
        if (m_stake && m_stake->transport() && m_stake->transport()->isReady(&why)) {
            connected = true;
        }
    }

    if (connected) {
        m_statusStateIcon->setStyleSheet(QStringLiteral("background-color: #2e9e46; border-radius: 6px;"));
        m_statusStateText->setText(tr("Connected"));
        m_statusStateText->setStyleSheet(QStringLiteral("color: #2e9e46; font-weight: bold;"));
    } else {
        m_statusStateIcon->setStyleSheet(QStringLiteral("background-color: #a8a8a8; border-radius: 6px;"));
        m_statusStateText->setText(tr("Disconnected"));
        m_statusStateText->setStyleSheet(QStringLiteral("color: #737373;"));
    }
}

void MainWindow::onResetSession()
{
  
    resetSessionState();
    onLogMessage(tr("Session reset"), false);
}

void MainWindow::resetSessionState()
{
    m_model->clear();
    // Live: keep the balance last reported by the server; simulation: start balance.
    m_engine->resetSession(m_engine->liveMode() ? m_engine->stats().balance : m_startBalance);
    m_fairPanel->refresh();
    refreshAfterStatsReset();
}

// Everything that has to follow a statistics reset (the bet history is not touched here).
void MainWindow::refreshAfterStatsReset()
{
    m_graph->clearSamples();

    // Running time restarts from zero; keep ticking if auto-bet is active.
    m_runAccumMs = 0;
    if (m_engine->isRunning() && !m_engine->isPaused())
        m_runElapsed.restart();
    else
        m_runElapsed.invalidate();
    updateRunTime();
    
    // --- FIX: Reset speed properly ---
    m_speedBetsAtWindowStart = 0;
    m_betsPerSecond = 0.0;
    if (m_engine->isRunning()) {
        m_speedTimer.restart(); // Keep it running so it updates with new bets
    } else {
        m_speedTimer.invalidate();
    }
    // ---------------------------------

    updateSessionWidgets();
    updateStatusWidgets();
    updateFilterCounts();
    updateChartLabels();
}

void MainWindow::onClearHistory()
{
    m_model->clear();
    onLogMessage(tr("Bet history cleared"), false);
}

void MainWindow::onRotateSeed()
{
    rotateSeed(false);
}

// fromScript: resetseed([clientSeed]) in a Lua / JavaScript strategy. Same rotation as
// the toolbar button, but with the script's client seed (random 10 characters when it
// gave none) and without switching to the Fairness tab on every call.
void MainWindow::rotateSeed(bool fromScript, const QString &clientSeed)
{
    // Scripts bring their own client seed; the toolbar button uses Options -> Game
    // (random when "Random client seed" is ticked, the typed seed otherwise).
    QString seedToUse;
    if (fromScript) {
        seedToUse = clientSeed.trimmed();
    } else {
        seedToUse = m_randomClientSeed ? StakeClient::randomClientSeed(10) : m_clientSeed.trimmed();
        if (!seedToUse.isEmpty())
            m_clientSeedPending = false; // manual bets need not rotate again for it
    }

    if (m_engine->liveMode()) {
        if (fromScript && m_rotatingSeed) {
            // A rotation (toolbar button) is still under way: report a failure so the
            // engine sends the script's request again in a moment.
            QTimer::singleShot(0, this, [this] { m_engine->seedRotationFinished(false); });
            return;
        }
        // Live: rotate the real seed pair through the GraphQL API. The client seed is the
        // script's own (resetseed("...")) or, for the toolbar button, the one from
        // Options -> Game; without any a random 10-character one is used.
        //if (m_rotatingSeed)
            //return;
        //if (m_engine->betInFlight()) {
           // onLogMessage(tr("A bet is in flight — try rotating the seed again in a moment."), true);
            //return;
        //}
        m_rotatingSeed = true;
        m_actRotate->setEnabled(false);
        QString seed = seedToUse;
        if (seed.isEmpty())
            seed = StakeClient::randomClientSeed(10);
        onLogMessage(tr("Rotating seed pair (new client seed: %1)…").arg(seed), false);
        m_stake->rotateSeedPair(seed);
        return;
    }

    // The Fair panel owns rotation so the revealed-seeds list stays in sync.
    if (!fromScript && m_tabs->isTabEnabled(m_tabs->indexOf(m_fairPanel)))
        m_tabs->setCurrentWidget(m_fairPanel);
    m_fairPanel->onRotate();
    // The server seed was rotated above; the client seed is the script's / the Options one.
    if (!seedToUse.isEmpty())
        m_provider->setClientSeed(seedToUse);
    m_fairPanel->refresh();
    if (fromScript)
        onLogMessage(tr("Seed pair rotated by script (client seed: %1)").arg(m_provider->clientSeed()),
                     false);
    else if (!seedToUse.isEmpty())
        onLogMessage(tr("Seed pair rotated (client seed: %1)").arg(m_provider->clientSeed()), false);
}

void MainWindow::onEngineStarted()
{
    if (m_luaDock)
        m_luaDock->setRunning(m_luaRunning);
    startRunClock();
    m_speedTimer.start();
    m_speedBetsAtWindowStart = m_engine->stats().bets;
    m_betsPerSecond = 0.0;
    m_actStart->setEnabled(false);
    m_actPause->setEnabled(true);
    m_actStop->setEnabled(true);
    m_actRollOnce->setEnabled(false);
    m_autoPanel->setRunning(true);
    m_manualPanel->setLocked(true);
    //m_statusStateIcon->setPixmap(QIcon(QStringLiteral(":/icons/filter-active.svg")).pixmap(14, 14));
    //m_statusStateText->setText(tr("Auto-betting"));
}

void MainWindow::onEngineStopped(const QString &reason)
{
    Q_UNUSED(reason);
    m_luaRunning = false;
    if (m_luaDock)
        m_luaDock->setRunning(false);
    stopRunClock();
    m_speedTimer.invalidate();
    m_betsPerSecond = 0.0;
    m_actStart->setEnabled(true);
    m_actStop->setEnabled(false);
    // Stopping while paused does not emit pausedChanged, so reset here.
    m_actPause->setEnabled(false);
    m_actPause->setText(tr("Pause auto-bet"));
    m_actPause->setToolTip(tr("Pause auto-bet (F6)"));
    m_actRollOnce->setEnabled(true);
    m_autoPanel->setRunning(false);
    m_manualPanel->setLocked(false);
    //m_statusStateIcon->setPixmap(QIcon(QStringLiteral(":/icons/filter-inactive.svg")).pixmap(14, 14));
    //m_statusStateText->setText(tr("Idle"));
    updateStatusWidgets();
}

void MainWindow::onPausedChanged(bool paused)
{
    if (m_engine->isRunning()) {
        if (paused)
            stopRunClock();
        else
            startRunClock();
    }
    m_actPause->setText(paused ? tr("Resume auto-bet") : tr("Pause auto-bet"));
    m_actPause->setToolTip(paused ? tr("Resume auto-bet (F6)") : tr("Pause auto-bet (F6)"));
    if (m_engine->isRunning()) {
        //m_statusStateIcon->setPixmap(QIcon(paused ? QStringLiteral(":/icons/pause-session.svg")
         //                                         : QStringLiteral(":/icons/filter-active.svg"))
         //                                .pixmap(14, 14));
        //m_statusStateText->setText(paused ? tr("Paused") : tr("Auto-betting"));
    }
    updateStatusWidgets();
}

void MainWindow::resolveBetIid(const QString &betId, std::function<void(const QString &)> done)
{
    if (betId.isEmpty() || !m_stake) {
        if (done)
            done(QString());
        return;
    }
    if (m_model->isResolved(betId)) {
        if (done)
            done(m_model->resolvedIid(betId));
        return;
    }
    if (m_iidLookups.contains(betId)) { // a lookup is already running for this row
        if (done)
            done(QString());            // caller falls back to the raw bet id
        return;
    }
    m_iidLookups.insert(betId);
    m_betTable->viewport()->setCursor(Qt::WaitCursor);

    QPointer<MainWindow> self(this);
    m_stake->lookupBetIid(betId, [self, betId, done](const QString &iid) {
        if (!self)
            return;
        self->m_iidLookups.remove(betId);
        if (self->m_iidLookups.isEmpty())
            self->m_betTable->viewport()->unsetCursor();

        QString clean = iid;
        clean.replace(QStringLiteral("house:"), QStringLiteral("casino:"));
        if (!clean.isEmpty()) {
            self->m_model->setResolvedIid(betId, clean);
            // Widen the column if the iid doesn't fit (capped at 250 px).
            const int needed = self->m_betTable->fontMetrics().horizontalAdvance(clean) + 16;
            if (needed > self->m_betTable->columnWidth(BetHistoryModel::ColBetId))
                self->m_betTable->setColumnWidth(BetHistoryModel::ColBetId, qMin(needed, 250));
        }
        if (done)
            done(clean);
    });
}

void MainWindow::onBetTableClicked(const QModelIndex &index)
{
    // Clicking an already highlighted row again removes the highlight.
    if (index.isValid() && m_betClickWasSelected) {
        m_betTable->clearSelection();
        m_betTable->setCurrentIndex(QModelIndex());
    }
    m_betClickWasSelected = false;

    if (!index.isValid() || index.column() != BetHistoryModel::ColBetId)
        return;
    const QString betId = m_model->betIdAt(index.row());
    if (betId.isEmpty() || m_model->isResolved(betId))
        return;                      // nothing to do / already revealed
    const QString clickedCasino = m_model->recordAt(index.row()).casino;
    if (clickedCasino.compare(QLatin1String("shuffle"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("goated"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("wolfbet"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("duel"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("paradice"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("pasino"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("tower"), Qt::CaseInsensitive) == 0
        || clickedCasino.compare(QLatin1String("winna"), Qt::CaseInsensitive) == 0)
        return;                      // only Stake's ids are looked up
    resolveBetIid(betId);
}

void MainWindow::onBetTableContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_betTable->indexAt(pos);
    if (!index.isValid())
        return;
    const QString betId = m_model->betIdAt(index.row());
    if (betId.isEmpty())
        return;

    const QPoint globalPos = m_betTable->viewport()->mapToGlobal(pos);

    // The row's own casino decides (the history can mix both); old rows without one
    // follow the active casino.
    const QString rowCasino = m_model->recordAt(index.row()).casino.trimmed().toLower();
    const bool shuffleRow = rowCasino.isEmpty() ? m_live.isShuffle()
                                                : rowCasino == QLatin1String("shuffle");
    const bool goatedRow = rowCasino.isEmpty() ? m_live.isGoated()
                                               : rowCasino == QLatin1String("goated");

    const bool wolfbetRow = rowCasino.isEmpty() ? m_live.isWolfbet()
                                                : rowCasino == QLatin1String("wolfbet");

    const bool duelRow = rowCasino.isEmpty() ? m_live.isDuel()
                                             : rowCasino == QLatin1String("duel");
    const bool paradiceRow = rowCasino.isEmpty() ? m_live.isParadice()
                                                 : rowCasino == QLatin1String("paradice");
    const bool pasinoRow = rowCasino.isEmpty() ? m_live.isPasino()
                                               : rowCasino == QLatin1String("pasino");
    const bool towerRow = rowCasino.isEmpty() ? m_live.isTower()
                                              : rowCasino == QLatin1String("tower");
    const bool winnaRow = rowCasino.isEmpty() ? m_live.isWinna()
                                              : rowCasino == QLatin1String("winna");
    // Duel / Paradice / Pasino / Tower / Winna: copy the round id (no public bet page is known).
    if (duelRow || paradiceRow || pasinoRow || towerRow || winnaRow) {
        QMenu menu;
        QAction *copyAct = menu.addAction(tr("Copy Bet id:  %1").arg(betId));
        if (menu.exec(globalPos) == copyAct)
            QApplication::clipboard()->setText(betId);
        return;
    }

    // Wolfbet: copy the bet id, or open its page on the site.
    if (wolfbetRow) {
        const QString betType = m_model->recordAt(index.row()).game == Game::Limbo
                                    ? QStringLiteral("limbo") : QStringLiteral("dice");
        const QUrl openUrl(QStringLiteral("https://%1/user/transactions?betType=%2&id=%3&modal=bet")
                               .arg(WolfbetTransport::host(), betType,
                                    QString::fromLatin1(QUrl::toPercentEncoding(betId))));
        QMenu menu;
        QAction *copyAct = menu.addAction(tr("Copy Bet id:  %1").arg(betId));
        QAction *openAct = menu.addAction(tr("Open in browser"));
        QAction *chosen = menu.exec(globalPos);
        if (chosen == copyAct)
            QApplication::clipboard()->setText(betId);
        else if (chosen == openAct)
            QDesktopServices::openUrl(openUrl);
        return;
    }

    // Goated: copy the bet id, or open its public bet modal on the site
    // (md-id = this row's bet id).
    if (goatedRow) {
        const QUrl openUrl(QStringLiteral("https://%1/?modal=bet&md-id=%2")
                               .arg(GoatedTransport::webHost(),
                                    QString::fromLatin1(QUrl::toPercentEncoding(betId))));
        QMenu menu;
        QAction *copyAct = menu.addAction(tr("Copy Bet id:  %1").arg(betId));
        QAction *openAct = menu.addAction(tr("Open in browser"));
        QAction *chosen = menu.exec(globalPos);
        if (chosen == copyAct)
            QApplication::clipboard()->setText(betId);
        else if (chosen == openAct)
            QDesktopServices::openUrl(openUrl);
        return;
    }

    // Shuffle: nothing to look up; the bet id is copiable and opens the public bet
    // modal on the configured Shuffle mirror (md-id = this row's bet id).
    if (shuffleRow) {
        // Use the Site / mirror currently set in Options (accepts "host" or "https://host/").
        QString host = m_live.shuffleSite.trimmed();
        const int scheme = host.indexOf(QLatin1String("://"));
        if (scheme >= 0)
            host = host.mid(scheme + 3);
        const int slash = host.indexOf(QLatin1Char('/'));
        if (slash >= 0)
            host = host.left(slash);
        if (host.isEmpty())
            host = QStringLiteral("shuffle.com");

        const QUrl openUrl(QStringLiteral("https://%1/?modal=bet&md-id=%2")
                               .arg(host, QString::fromLatin1(QUrl::toPercentEncoding(betId))));

        QMenu menu;
        QAction *copyAct = menu.addAction(tr("Copy Bet id:  %1").arg(betId));
        QAction *openAct = menu.addAction(tr("Open in browser"));
        QAction *chosen = menu.exec(globalPos);
        if (chosen == copyAct)
            QApplication::clipboard()->setText(betId);
        else if (chosen == openAct)
            QDesktopServices::openUrl(openUrl);
        return;
    }

    // Stake: always resolve the iid on every right click (cached once found; a failed
    // lookup is retried next time), then show the menu.
    auto showMenu = [this, betId, globalPos](const QString &iid) {
        const QString site = m_live.site;
        const QString copyText = iid.isEmpty() ? betId : iid;
        const QUrl openUrl(iid.isEmpty()
                               ? QStringLiteral("https://%1/?betId=%2&modal=bet").arg(site, betId)
                               : QStringLiteral("https://%1/?modal=bet&iid=%2").arg(site, iid));

        QMenu menu;
        QAction *copyAct = menu.addAction(iid.isEmpty() ? tr("Copy Bet ID:  %1").arg(betId)
                                                        : tr("Copy IID:  %1").arg(iid));
        QAction *openAct = menu.addAction(tr("Open in browser"));
        QAction *chosen = menu.exec(globalPos);
        if (chosen == copyAct)
            QApplication::clipboard()->setText(copyText);
        else if (chosen == openAct)
            QDesktopServices::openUrl(openUrl);
    };

    // (menu is shown from the event loop, not from inside the network callback)
    resolveBetIid(betId, [this, showMenu](const QString &iid) {
        QTimer::singleShot(0, this, [showMenu, iid] { showMenu(iid); });
    });
}

void MainWindow::onBetPlaced(const BetRecord &record)
{
    m_model->addBet(record);
    m_manualPanel->onBetResult(record);
    m_graph->addSample(m_engine->stats().profit);
    updateSessionWidgets();
    updateStatusWidgets();
    updateFilterCounts();
    updateChartLabels();
	
	//if (m_live.enabled && m_engine->stats().bets % 50 == 0) {
       // m_stake->fetchBalances();
    // }
}

void MainWindow::onLogMessage(const QString &message, bool isError)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    //m_logLineLabel->setText(QStringLiteral("%1  %2").arg(stamp, message));
    //m_logLineLabel->setToolTip(message);
    if (m_logWidget)
        m_logWidget->appendLog(message, isError);
}

void MainWindow::onSearchEdited(const QString &text)
{
    m_model->setSearchText(text.trimmed());
}

void MainWindow::onSwitchToChart()
{
    // Statistics button: toggles the Profit Chart. If the chart is already showing in the
    // bottom panel, hide the panel again; otherwise show the panel (if it was hidden), select
    // the tab and re-fit the pane (also when that tab was already selected but the pane had
    // been dragged shut, since currentChanged does not fire then).
    constexpr int kChartTab = 2; // Manual Bet, Auto Bet, Profit Chart, Provably Fair
    const bool panelShown = m_actShowTabs && m_actShowTabs->isChecked();
    const bool paneOpen = centralWidget()->height() > 40; // not dragged shut
    if (panelShown && paneOpen && m_tabs->currentIndex() == kChartTab) {
        m_actShowTabs->setChecked(false);
        return;
    }
    if (m_actShowTabs && !m_actShowTabs->isChecked())
        m_actShowTabs->setChecked(true);
    m_tabs->setCurrentIndex(kChartTab);
    onTabChanged(kChartTab);
}

// When switching to Provably Fair / Auto Bet, resize the bottom pane so the
// page's content fits exactly. FairPanel gets a safety floor (its hint is 0
// on the very first switch, before it has laid itself out); AutoBetPanel uses
// QSizePolicy::Maximum, so its sizeHint() is already the tight natural height.
void MainWindow::onTabChanged(int index)
{
    if (!m_tabs->widget(index))
        return;
    // Size the panel to the new tab's content (again once the layout has settled, for late
    // size hints). Docked panels are never resized for it - only the user resizes those.
    updateCentralHeight();
    QTimer::singleShot(0, this, [this] {
        updateCentralHeight();
        fitTopDock(true); // a tab was clicked: show all of it
    });
}

// Height the bottom panel needs for its current tab: tab bar + page content (+ the
// horizontal scroll bar when part of the panel is covered).
int MainWindow::bottomPanelHeight() const
{
    QWidget *page = m_tabs ? m_tabs->currentWidget() : nullptr;
    if (!page)
        return 0;
    if (QLayout *l = page->layout())
        l->activate();
    const int tabBarH = m_tabs->tabBar()->sizeHint().height();
    const int contentH = std::max({page->sizeHint().height(),
                                   page->minimumSizeHint().height(),
                                   page->minimumHeight(),
                                   80});
    int needed = contentH + tabBarH + 8; // small buffer for the pane border
    if (auto *holder = dynamic_cast<FixedWidthHolder *>(m_tabs->parentWidget()))
        needed += holder->scrollBarHeight();
    return needed;
}

// The bottom panel has a fixed height: the height of its current tab's content, never more
// (it can still be made smaller by dragging a separator into it; the page is then clipped).
// It is placed at the bottom of the central area, right on whatever is docked below it (Log,
// Bet List ...), so it moves with that dock's separator. A panel docked at the top of the
// window fills down to it (see fitTopDock()); with nothing docked at the top, the spare
// height stays empty above it. Panels docked at the bottom are never resized automatically.
void MainWindow::updateCentralHeight()
{
    QWidget *central = centralWidget();
    if (!central || !m_tabs)
        return;
    const bool panelShown = !m_actShowTabs || m_actShowTabs->isChecked();
    m_panelHeight = panelShown ? std::max(1, bottomPanelHeight()) : 0;
    positionBottomPanel();
    if (central->maximumHeight() != QWIDGETSIZE_MAX)
        central->setMaximumHeight(QWIDGETSIZE_MAX); // never capped (that let the Log grow)
    QTimer::singleShot(0, this, [this] { fitTopDock(false); });
}

// The Log keeps the height the user gave it: only dragging its separator (or moving / showing
// it) changes that height. Whatever else re-lays the window out - the bet list popped out or
// docked back, a bottom-panel tab clicked, the window resized - the Log is put back to it.
void MainWindow::rememberLogHeight()
{
    if (m_logDock && !m_logDock->isHidden() && !m_logDock->isFloating())
        m_logHeight = m_logDock->height();
}

void MainWindow::keepLogHeight()
{
    if (m_separatorDrag || m_logHeight <= 0 || !m_logDock || m_logDock->isHidden()
        || m_logDock->isFloating())
        return;
    // Only a Log docked at the bottom keeps a fixed height; at the top it fills down to the
    // bottom panel (fitTopDock()).
    if (dockWidgetArea(m_logDock) != Qt::BottomDockWidgetArea)
        return;
    if (m_logDock->height() != m_logHeight)
        resizeDocks({m_logDock}, {m_logHeight}, Qt::Vertical);
}

void MainWindow::scheduleKeepLogHeight()
{
    QTimer::singleShot(0, this, &MainWindow::keepLogHeight);
    QTimer::singleShot(60, this, &MainWindow::keepLogHeight); // again once the layout settled
}

// Places the panel at the bottom of the central area, at its content height (or the whole
// area when that is smaller).
// The panel docked at the top of the window (Bet List, Log, ... or a tab group of them) fills
// all the height down to the bottom panel, so the bottom panel is never taller than its
// content: spare height goes to the top panel. With growPanel (a tab was clicked) a bottom
// panel that is shorter than its content also gets the missing height back from the top panel
// (which keeps a small strip); otherwise a bottom panel the user dragged smaller stays so. Only the separator between the top panel and the bottom
// panel moves - panels docked at the bottom keep their size. Not during a separator drag
// (it is applied when the mouse is released, so the bottom panel cannot stay taller than its
// content).
void MainWindow::fitTopDock(bool growPanel)
{
    if (m_separatorDrag || !m_centralArea || !m_tabs)
        return;
    QDockWidget *top = nullptr;
    for (QDockWidget *dock : findChildren<QDockWidget *>()) {
        if (dock->isHidden() || dock->isFloating() || !dock->isVisible()
            || dockWidgetArea(dock) != Qt::TopDockWidgetArea)
            continue;
        if (!top || dock->height() > top->height())
            top = dock;
    }
    if (!top)
        return; // nothing above the panel: the spare height stays empty
    const int diff = m_centralArea->height() - m_panelHeight; // > 0: empty, < 0: clipped
    if (diff == 0 || (diff < 0 && !growPanel))
        return;
    const int topH = top->height();
    const int target = std::max(60, topH + diff);
    if (target != topH)
        resizeDocks({top}, {target}, Qt::Vertical);
}

void MainWindow::positionBottomPanel()
{
    if (!m_centralArea || !m_tabs)
        return;
    QWidget *holder = m_tabs->parentWidget();
    const int areaH = m_centralArea->height();
    // Always its content height, at the bottom (right on the dock below it, so it moves with
    // that dock's separator); spare height stays empty above it.
    const int h = std::min(areaH, m_panelHeight);
    const QRect r(0, areaH - h, m_centralArea->width(), h);
    if (holder->geometry() != r)
        holder->setGeometry(r);
}

// Floating (undocked) panels: by default Qt makes them tool windows owned by the main
// window, so they always stay on top of it. Here they become independent windows instead:
// clicking the main window brings it in front of them, and each floating panel gets its own
// taskbar button to bring it back. Docked panels are unaffected.
void MainWindow::setupFloatingDocks()
{
    for (QDockWidget *dock : findChildren<QDockWidget *>()) {
        connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating) {
            if (floating)
                detachFloatingDock(dock, true); // just popped out: bring it to the front
        });
        // Docks appearing / moving / floating change what can take the spare height.
        const auto refit = [this] {
            QTimer::singleShot(0, this, &MainWindow::updateCentralHeight);
        };
        connect(dock, &QDockWidget::topLevelChanged, this, refit);
        connect(dock, &QDockWidget::dockLocationChanged, this, refit);
        connect(dock, &QDockWidget::visibilityChanged, this, refit);
        dock->installEventFilter(this); // Show: see eventFilter()
    }
}

void MainWindow::detachFloatingDock(QDockWidget *dock, bool bringToFront)
{
#ifdef Q_OS_WIN
    QPointer<QDockWidget> guard(dock);
    QTimer::singleShot(0, this, [this, guard, bringToFront] {
        if (!guard || !guard->isFloating() || !guard->isVisible())
            return;
        // Still being dragged (it may yet be docked again): try once the mouse is released.
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
            QTimer::singleShot(150, this, [this, guard, bringToFront] {
                if (guard)
                    detachFloatingDock(guard, bringToFront);
            });
            return;
        }
        const HWND hwnd = reinterpret_cast<HWND>(guard->winId());
        // No owner: the window is no longer kept above the main window.
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);
        // A normal (not tool) window with its own taskbar button, so it can be found again
        // once it is behind the main window. The taskbar picks this up on the next show.
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const LONG_PTR want = (ex & ~LONG_PTR(WS_EX_TOOLWINDOW)) | WS_EX_APPWINDOW;
        if (want != ex) {
            ShowWindow(hwnd, SW_HIDE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, want);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                             | SWP_FRAMECHANGED);
            ShowWindow(hwnd, SW_SHOW);
        }
        // Just popped out: without an owner Windows may leave it under the main window (the
        // mouse click that dropped it), so bring it to the front once and focus it. It stays
        // there until the main window is clicked. Never done again later, so it does not
        // take the focus back from the main window.
        if (bringToFront) {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            guard->raise();
            guard->activateWindow();
        }
    });
#else
    Q_UNUSED(dock);
    Q_UNUSED(bringToFront);
#endif
}

// True when the bet list is docked above or below the bottom panel, so the two share the
// window height (like the old splitter); false when it is floating, hidden or on a side.
bool MainWindow::betListStacked() const
{
    if (!m_betListDock || m_betListDock->isHidden() || m_betListDock->isFloating())
        return false;
    // Tabbed together with another panel (e.g. the Log): resizing it would resize that panel
    // too, so it does not share the height with the bottom panel.
    if (!tabifiedDockWidgets(m_betListDock).isEmpty())
        return false;
    const Qt::DockWidgetArea area = dockWidgetArea(m_betListDock);
    return area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea;
}

void MainWindow::onToggleDarkTheme(bool enabled)
{
    applyTheme(enabled);

    // One theme for every window: saved outside the per-window groups and applied to all
    // open windows at once (setChecked() only re-enters here when the value changes).
    QSettings().setValue(QLatin1String(kThemeKey), enabled);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        auto *other = qobject_cast<MainWindow *>(w);
        if (other && other != this && other->m_actDarkTheme
            && other->m_actDarkTheme->isChecked() != enabled)
            other->m_actDarkTheme->setChecked(enabled);
    }
}

void MainWindow::onOpenFile()
{
    // "+" button: open another, independent main window.
    // It gets the lowest window number that is not open (main window = 0, the first "+"
    // window = 1, …) and loads that window's own saved data, so after a restart the
    // second window comes back exactly as it was left. Everything it changes is saved to
    // its own group again.
    // A number that was never used before starts with its own default data (nothing is
    // copied from this window: layout, scripts, auto-bet, conditions, strategies and Options
    // are all its own) and a fresh browser profile: logged out of every site. From then on it
    // saves and loads its own data. Only the theme is shared by all windows.
    const int slot = freeSlot();
    const QString group = WindowSettings::groupFor(slot);
    bool hasData = false;
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Windows"));
        hasData = settings.childGroups().contains(QString::number(slot));
    }
    Q_UNUSED(group);
    if (!hasData) {
        // First use of this window number: nothing is copied from this window. It starts
        // with its own default data (registry group "Windows/<slot>", written when it is
        // closed) and an empty strategy file of its own (strategies_<n>.json).
        if (!QFile::exists(ConditionIo::strategiesPath(slot)))
            ConditionIo::saveStrategies(StrategyMap(), slot);
    }
    auto *window = new MainWindow(slot, !hasData); // the theme is shared: see kThemeKey

    if (!hasData) {
        // Cascaded so it doesn't sit exactly on top of this one. A window with saved data
        // keeps its own position.
        window->move(pos() + QPoint(30, 30));
    }
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::onOpenConditionBuilder()
{
    ConditionBuilderDialog dialog(m_conditions, m_strategies, m_activeStrategy, this);
    dialog.setStrategiesSlot(m_slot); // each window has its own strategy file
    dialog.setWindowTitle(tr("Condition Builder — %1").arg(gameName(m_game).toUpper()));
    if (dialog.exec() != QDialog::Accepted) {
        // The dialog autosaves this window's strategy file as you edit, even if you cancel;
        // re-read it so the in-memory copy matches the file.
        if (QFile::exists(ConditionIo::strategiesPath(m_slot)))
            m_strategies = ConditionIo::loadStrategies(m_slot);
        return;
    }

    m_conditions = dialog.result();
    m_strategies = dialog.resultStrategies();
    m_activeStrategy = dialog.resultStrategyName();
    saveConditions();
    ConditionIo::saveStrategies(m_strategies, m_slot);
    m_autoPanel->setConditionCount(m_conditions.size());

    QString msg = tr("Conditions saved (%1 blocks)").arg(m_conditions.size());
    if (!m_activeStrategy.isEmpty())
        msg += tr(" — strategy: \"%1\"").arg(m_activeStrategy);
    onLogMessage(msg, false);
}

void MainWindow::loadConditions()
{
    ScopedSettings settings(settingsGroup());
    const QByteArray raw = settings.value(QStringLiteral("Conditions/blocks")).toByteArray();
    m_conditions = ConditionIo::blocksFromJson(QJsonDocument::fromJson(raw).array());
    m_activeStrategy = settings.value(QStringLiteral("Conditions/activeStrategy")).toString();
    m_strategies = ConditionIo::loadStrategies(m_slot);
}

void MainWindow::saveConditions()
{
    ScopedSettings settings(settingsGroup());
    settings.setValue(QStringLiteral("Conditions/blocks"),
                      QJsonDocument(ConditionIo::blocksToJson(m_conditions)).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("Conditions/activeStrategy"), m_activeStrategy);
}

void MainWindow::onOpenOptions()
{
    OptionsDialog dialog(this);
    m_optionsDialog = &dialog;
    const bool darkBefore = m_actDarkTheme->isChecked();
    dialog.setDarkTheme(darkBefore);
    // Picking a theme applies it at once (to every window); Cancel restores the old one.
    connect(&dialog, &OptionsDialog::darkThemeChanged, this,
            [this](bool dark) { m_actDarkTheme->setChecked(dark); });
    dialog.setCurrencyTag(m_currencyTag);
    dialog.setStartBalance(m_startBalance);
    dialog.setClientSeed(m_clientSeed);
    dialog.setRandomClientSeed(m_randomClientSeed);
    dialog.setBetDelayMs(m_betDelayMs);
    dialog.setRetryNoBalance(m_retryNoBalance);
    {
        QStringList currencies;
        for (int i = 0; i < LiveSettings::kCasinoCount; ++i)
            currencies << (i == int(m_live.casino) ? m_live.currency : m_currencyByCasino[i]);
        dialog.setCasinoCurrencies(currencies);
    }
    // Loading the currencies by itself reconfigures the connection (like "Log in"): not while
    // a bet or auto-bet is running.
    dialog.setAutoFetchEnabled(!m_engine->isRunning() && !m_engine->betInFlight());
    dialog.setLiveSettings(m_live); // shows the typed currency of the active casino
    dialog.setExtensionStatus(m_ext->isListening(), m_ext->connectedHosts());

    connect(m_ext, &IStakeTransport::readyChanged, &dialog, [this, &dialog](bool) {
        dialog.setExtensionStatus(m_ext->isListening(), m_ext->connectedHosts());
    });
    connect(&dialog, &OptionsDialog::loginRequested, this, [this, &dialog] {
        const LiveSettings cur = dialog.liveSettings();
        if (!cur.activeUsesExtension()) {
            loginFromDialog(dialog);
            return;
        }
        // "Browser extension": nothing is typed in; the login is read from the casino's tab.
        QString err;
        if (!configureConnection(cur, &err)) { // starts the extension server
            dialog.setLoginStatus(err, false);
            return;
        }
        dialog.setExtensionStatus(m_ext->isListening(), m_ext->connectedHosts());
        const QStringList hosts = extensionTabHosts(cur, cur.casino);
        if (!m_ext->isConnectedTo(hosts)) {
            dialog.setLoginStatus(tr("Open %1 in your browser (with the extension) and log in "
                                     "there — this connects by itself.")
                                      .arg(hosts.value(0)),
                                  false);
            return;
        }
        dialog.setLoginStatus(tr("Reading the login from the %1 tab…").arg(hosts.value(0)), true);
        QPointer<OptionsDialog> dlg(&dialog);
        grabExtensionCredentials(cur.casino, cur, [this, dlg](const LiveSettings &got, const QString &error) {
            if (!dlg)
                return;
            if (!error.isEmpty()) {
                dlg->setLoginStatus(error, false);
                return;
            }
            dlg->setGrabbedCredentials(got); // shows what was read; OK saves it
            loginFromDialog(*dlg);
        });
    });

    // "Site / mirror" is saved as soon as it is typed (also when the dialog is cancelled).
    connect(&dialog, &OptionsDialog::siteEdited, this, [this, &dialog] {
        const LiveSettings cur = dialog.liveSettings();
        ScopedSettings settings(settingsGroup());
        settings.beginGroup(QStringLiteral("Live"));
        if (cur.isDuel()) {
            m_live.duelSite = cur.duelSite;
            settings.setValue(QStringLiteral("duelSite"), m_live.duelSite);
        } else if (cur.isShuffle()) {
            m_live.shuffleSite = cur.shuffleSite;
            settings.setValue(QStringLiteral("shuffleSite"), m_live.shuffleSite);
        } else if (cur.casino == LiveSettings::Casino::Stake) {
            m_live.site = cur.site;
            settings.setValue(QStringLiteral("site"), m_live.site);
        }
        settings.endGroup();
    });

    // A login window filled in new data: keep it now (also when Options is closed without OK or
    // the app is closed), so the next start does not come back with the previous login.
    connect(&dialog, &OptionsDialog::credentialsCaptured, this, [this](const LiveSettings &got) {
        if (m_live.sameCredentials(got.casino, got))
            return;
        m_live.copyCredentials(got.casino, got);
        ScopedSettings settings(settingsGroup());
        saveOptionsSettings(settings);
        settings.sync();
    });

    connect(&dialog, &OptionsDialog::dataCleared, this, [this, &dialog] {
        m_discardSettings = true;
        dialog.reject();
    });

    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (m_discardSettings) {
        close(); // closeEvent skips saving so nothing is written back
        return;
    }
    if (!accepted) {
        m_actDarkTheme->setChecked(darkBefore); // undo a theme previewed in the dialog
        // A login test may have reconfigured the connection with unsaved values.
        configureConnection(m_live);
        updateShuffleBackground(m_live); // closes a socket a login test may have left open
        return;
    }

    m_currencyTag = dialog.currencyTag();
    m_startBalance = dialog.startBalance();
    m_manualPanel->setStartBalance(m_startBalance);
    if (m_clientSeed != dialog.clientSeed() || m_randomClientSeed != dialog.randomClientSeed())
        m_clientSeedPending = true; // the next manual bet / auto-bet uses the new setting
    m_clientSeed = dialog.clientSeed();
    m_randomClientSeed = dialog.randomClientSeed();
    m_betDelayMs = dialog.betDelayMs();
    m_engine->setBetDelayMs(m_betDelayMs);
    m_retryNoBalance = dialog.retryNoBalance();
    m_engine->setRetryNoBalance(m_retryNoBalance);
    m_actDarkTheme->setChecked(dialog.darkTheme());

    if (m_engine->isRunning() || m_engine->betInFlight()) {
        configureConnection(m_live);
        onLogMessage(tr("Live betting settings are not changed while a bet or auto-bet is running."),
                     true);
    } else {
        // Casino selector in Options: each casino keeps its own bet currency.
        const QStringList currencies = dialog.casinoCurrencies();
        for (int i = 0; i < LiveSettings::kCasinoCount && i < currencies.size(); ++i)
            m_currencyByCasino[i] = currencies.at(i);
        const LiveSettings next = dialog.liveSettings();
        const bool casinoChanged = next.casino != m_live.casino;
        applyLiveSettings(next);
        updateCasinoMenu();
        if (casinoChanged)
            onLogMessage(tr("Casino: %1").arg(casinoDisplayName(m_live.casino)), false);
    }

    // Save right away (not only on exit), so the API key / tokens survive a crash.
    {
        ScopedSettings settings(settingsGroup());
        saveOptionsSettings(settings);
        settings.sync();
    }

    updateSessionWidgets();
    updateStatusWidgets();
    onLogMessage(tr("Options applied (currency: %1)").arg(currencyLabel()), false);
}

// ---------------------------------------------------------------- live betting

// "Log in" in Options with every input in place: applies them and reads the balances.
void MainWindow::loginFromDialog(OptionsDialog &dialog)
{
    QString err;
    if (!configureConnection(dialog.liveSettings(), &err)) {
        dialog.setLoginStatus(err, false);
        return;
    }
    dialog.setExtensionStatus(m_ext->isListening(), m_ext->connectedHosts());

    // Shuffle direct mode talks to Shuffle straight from the app (HTTPS + websocket) and
    // never uses the hidden WebView2 browser (which only loads Stake's site), so there is
    // nothing to wait for. Waiting on it made the login hang on "Loading browser…"
    // whenever that unrelated Stake page was slow or failed to load.
    const LiveSettings cur = dialog.liveSettings();
    // Duel: its requests run in the hidden Duel browser; the pageLoaded handler in
    // ensureDuelBrowser() fetches the balances once it is there.
    if (cur.isDuel() && !cur.duelExtension && m_duelBrowser && !m_duelBrowser->isPageLoaded()) {
        dialog.setLoginStatus(tr("Loading the Duel browser…"), true);
        return;
    }
    if (cur.isDuel() && !cur.duelExtension && !m_duelBrowser) {
        dialog.setLoginStatus(tr("Duel needs the WebView2 Runtime (Microsoft Edge WebView2)."), false);
        return;
    }
    const bool needsBrowser = !cur.isDirect() && !cur.extensionMode;
    // Stake cookie mode: the browser was just (re)created and is still loading: wait for
    // it, but not forever.
    if (needsBrowser && m_hiddenBrowser && !m_hiddenBrowser->isPageLoaded()) {
        dialog.setLoginStatus(tr("Loading browser…"), true);
        QPointer<OptionsDialog> dlg(&dialog);
        auto done = std::make_shared<bool>(false);
        connect(m_hiddenBrowser, &CookieGrabberDialog::pageLoaded, dlg.data(),
                [this, done] {
                    if (*done)
                        return;
                    *done = true;
                    m_stake->fetchBalances();
                },
                Qt::SingleShotConnection); // Qt 6.0+
        // Give up after 30 s so the dialog never stays stuck on "Loading browser…".
        QTimer::singleShot(30000, dlg.data(), [dlg, done] {
            if (*done || !dlg)
                return;
            *done = true;
            dlg->setLoginStatus(
                tr("The browser page did not finish loading (Cloudflare check or network "
                   "problem?). Press Log in to try again."),
                false);
        });
        return;
    }
    m_stake->fetchBalances(); // answer arrives in onBalancesReady/Failed
}

// Sites whose tab can serve casino `c` (Stake: any Stake domain as a fallback).
QStringList MainWindow::extensionTabHosts(const LiveSettings &s, LiveSettings::Casino c) const
{
    QStringList hosts = s.extensionHosts(c);
    if (c == LiveSettings::Casino::Stake)
        hosts += ExtensionTransport::stakeHosts();
    return hosts;
}

// Reads casino `c`'s login data from its tab: the same values the "Open … & Log in" windows
// read, so nothing has to be typed in. `done` gets `base` with them filled in.
void MainWindow::grabExtensionCredentials(LiveSettings::Casino c, const LiveSettings &base, GrabDone done)
{
    auto out = std::make_shared<LiveSettings>(base);
    out->casino = c;
    const QStringList hosts = extensionTabHosts(base, c);
    const QString site = hosts.value(0);
    QPointer<MainWindow> self(this);
    const auto notLoggedIn = [site](const QString &what) {
        return tr("Not logged in on the %1 tab (%2 not found) — log in there in your browser.")
            .arg(site, what);
    };

    switch (c) {
    case LiveSettings::Casino::Goated:
        // localStorage "g3_token" (a plain or a JSON-quoted string)
        m_ext->readStorage(hosts, QStringLiteral("g3_token"),
                           [self, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString token = GoatedTransport::cleanToken(v);
            if (token.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("g3_token")));
            out->goatedToken = token;
            done(*out, QString());
        });
        return;

    case LiveSettings::Casino::Wolfbet:
        // "token" cookie: URL-encoded JSON {"value":"<jwt>","expire":...}
        m_ext->readCookie(hosts, QStringLiteral("token"),
                          [self, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString decoded = QUrl::fromPercentEncoding(v.toUtf8());
            QString token = QJsonDocument::fromJson(decoded.toUtf8()).object()
                                .value(QStringLiteral("value")).toString().trimmed();
            if (token.isEmpty() && !decoded.trimmed().startsWith(QLatin1Char('{')))
                token = decoded.trimmed(); // a plain token
            if (token.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("the \"token\" cookie")));
            out->wolfbetToken = token;
            done(*out, QString());
        });
        return;

    case LiveSettings::Casino::Paradice:
        // localStorage "token" (a plain or a JSON-quoted JWT)
        m_ext->readStorage(hosts, QStringLiteral("token"),
                           [self, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString token = ParadiceTransport::cleanToken(v);
            if (token.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("token")));
            out->paradiceToken = token;
            done(*out, QString());
        });
        return;

    case LiveSettings::Casino::Pasino:
        // localStorage "token" (64 hex characters, plain or JSON-quoted)
        m_ext->readStorage(hosts, QStringLiteral("token"),
                           [self, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString token = PasinoTransport::cleanToken(v);
            if (token.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("token")));
            out->pasinoToken = token;
            done(*out, QString());
        });
        return;

    case LiveSettings::Casino::Tower:
        // "__tt" cookie (the Bearer token). cf_clearance is not needed: the tab sends its own.
        m_ext->readCookie(hosts, QStringLiteral("__tt"),
                          [self, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString token = TowerTransport::cleanToken(QUrl::fromPercentEncoding(v.toUtf8()));
            if (token.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("the \"__tt\" cookie")));
            out->towerToken = token;
            done(*out, QString());
        });
        return;

    case LiveSettings::Casino::Winna:
        // localStorage "wn-antifraud-device-id" ({"id":…}) + the "connect.sid" cookie (httpOnly;
        // shown in Options only: in extension mode the tab sends its own cookies)
        m_ext->readStorage(hosts, QStringLiteral("wn-antifraud-device-id"),
                           [this, self, hosts, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString device = WinnaTransport::cleanDeviceId(v);
            if (!device.isEmpty())
                out->winnaDeviceId = device;
            m_ext->readCookie(hosts, QStringLiteral("connect.sid"),
                              [self, out, done, notLoggedIn](bool ok2, const QString &v2, const QString &err2) {
                if (!self)
                    return;
                if (!ok2)
                    return done(*out, err2);
                const QString session = WinnaTransport::cleanSession(v2);
                if (session.isEmpty())
                    return done(*out, notLoggedIn(QStringLiteral("the \"connect.sid\" cookie")));
                out->winnaSession = session;
                done(*out, QString());
            });
        });
        return;

    case LiveSettings::Casino::Duel:
        // localStorage "security:uuid" + the "duel" and "cf_clearance" cookies (httpOnly; shown
        // in Options only: in extension mode the tab sends them by itself)
        m_ext->readStorage(hosts, QStringLiteral("security:uuid"),
                           [this, self, hosts, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QString uuid = DuelTransport::cleanUuid(v);
            if (uuid.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("security:uuid")));
            out->duelUuid = uuid;
            m_ext->readCookie(hosts, QStringLiteral("duel"),
                              [this, self, hosts, out, done](bool ok2, const QString &v2, const QString &) {
                if (!self)
                    return;
                if (ok2 && !v2.trimmed().isEmpty())
                    out->duelToken = DuelTransport::cleanCookie(v2, QStringLiteral("duel"));
                m_ext->readCookie(hosts, QStringLiteral("cf_clearance"),
                                  [self, out, done](bool ok3, const QString &v3, const QString &) {
                    if (!self)
                        return;
                    if (ok3 && !v3.trimmed().isEmpty())
                        out->duelClearance = DuelTransport::cleanCookie(v3, QStringLiteral("cf_clearance"));
                    done(*out, QString()); // both cookies are optional here
                });
            });
        });
        return;

    case LiveSettings::Casino::Shuffle:
        // localStorage "persist:root" -> auth (JSON string) -> accessToken, plus
        // CORRELATION_USER_ID and BROWSER_ID
        m_ext->readStorage(hosts, QStringLiteral("persist:root"),
                           [this, self, hosts, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            const QJsonObject root = QJsonDocument::fromJson(v.toUtf8()).object();
            const QJsonObject auth =
                QJsonDocument::fromJson(root.value(QStringLiteral("auth")).toString().toUtf8()).object();
            const QString token = auth.value(QStringLiteral("accessToken")).toString().trimmed();
            if (token.isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("accessToken")));
            out->shuffleToken = token;
            m_ext->readStorage(hosts, QStringLiteral("CORRELATION_USER_ID"),
                               [this, self, hosts, out, done, notLoggedIn](bool ok2, const QString &v2, const QString &err2) {
                if (!self)
                    return;
                if (!ok2)
                    return done(*out, err2);
                if (v2.trimmed().isEmpty())
                    return done(*out, notLoggedIn(QStringLiteral("CORRELATION_USER_ID")));
                out->correlationUserId = v2.trimmed();
                m_ext->readStorage(hosts, QStringLiteral("BROWSER_ID"),
                                   [self, out, done, notLoggedIn](bool ok3, const QString &v3, const QString &err3) {
                    if (!self)
                        return;
                    if (!ok3)
                        return done(*out, err3);
                    if (v3.trimmed().isEmpty())
                        return done(*out, notLoggedIn(QStringLiteral("BROWSER_ID")));
                    out->browserId = v3.trimmed();
                    done(*out, QString());
                });
            });
        });
        return;

    default:
        // Stake: the "session" cookie (httpOnly) holds the token the site sends as x-access-token;
        // "cf_clearance" (optional) is shown in Options.
        m_ext->readCookie(hosts, QStringLiteral("session"),
                          [this, self, hosts, out, done, notLoggedIn](bool ok, const QString &v, const QString &err) {
            if (!self)
                return;
            if (!ok)
                return done(*out, err);
            if (v.trimmed().isEmpty() && out->apiKey.trimmed().isEmpty())
                return done(*out, notLoggedIn(QStringLiteral("the \"session\" cookie")));
            if (!v.trimmed().isEmpty())
                out->apiKey = v.trimmed(); // else: keep the API key typed in Options
            m_ext->readCookie(hosts, QStringLiteral("cf_clearance"),
                              [self, out, done](bool ok2, const QString &v2, const QString &) {
                if (!self)
                    return;
                if (ok2 && !v2.trimmed().isEmpty())
                    out->clearance = v2.trimmed();
                done(*out, QString()); // cf_clearance is optional
            });
        });
        return;
    }
}

void MainWindow::applyGrabbedCredentials(LiveSettings::Casino c, const LiveSettings &grabbed)
{
    if (m_live.sameCredentials(c, grabbed))
        return;
    m_live.copyCredentials(c, grabbed);
    configureConnection(m_live);
    ScopedSettings settings(settingsGroup());
    saveOptionsSettings(settings); // survives a crash, like Options -> OK
    settings.sync();
}

void MainWindow::refreshExtensionLogin(LiveSettings::Casino c)
{
    if (!m_live.usesExtension(c) || m_optionsDialog) // Options reads it by itself
        return;
    if (!m_ext->isConnectedTo(extensionTabHosts(m_live, c)))
        return;
    QPointer<MainWindow> self(this);
    grabExtensionCredentials(c, m_live, [self, c](const LiveSettings &got, const QString &error) {
        if (!self || !self->m_live.usesExtension(c))
            return;
        const bool active = self->m_live.casino == c;
        if (!error.isEmpty()) {
            if (active && self->m_live.enabled)
                self->onLogMessage(error, true);
            return;
        }
        self->applyGrabbedCredentials(c, got);
        if (!self->m_live.enabled)
            return;
        if (active) {
            self->m_stake->fetchBalances();
            return;
        }
        switch (c) { // background casino: fill its cache
        case LiveSettings::Casino::Goated:
            if (self->m_goated->isReady())
                self->m_goated->requestBalances();
            break;
        case LiveSettings::Casino::Wolfbet:
            if (self->m_wolfbet->isReady())
                self->m_wolfbet->requestBalances();
            break;
        case LiveSettings::Casino::Paradice:
            if (self->m_paradice->isReady())
                self->m_paradice->requestBalances();
            break;
        case LiveSettings::Casino::Pasino:
            if (self->m_pasino->isReady())
                self->m_pasino->requestBalances();
            break;
        case LiveSettings::Casino::Tower:
            if (self->m_tower->isReady())
                self->m_tower->requestBalances();
            break;
        case LiveSettings::Casino::Winna:
            if (self->m_winna->isReady())
                self->m_winna->requestBalances();
            break;
        case LiveSettings::Casino::Duel:
            if (self->m_duel->isReady())
                self->m_duel->requestBalances();
            break;
        case LiveSettings::Casino::Shuffle:
            self->updateShuffleBackground(self->m_live, false);
            break;
        default:
            self->requestStakeBackgroundBalance();
            break;
        }
    });
}

// The hidden WebView2 browser (Stake's cookie mode) is created once and kept open, also
// while Shuffle is the active casino, so switching back to Stake needs no page load.
void MainWindow::ensureHiddenBrowser(const LiveSettings &s)
{
    StakeCredentials cred;
    cred.site = s.site;
    cred.apiKey = s.apiKey;
    cred.clearance = s.clearance;
    cred.userAgent = s.userAgent;

    if (!m_webTransport) {
        m_webTransport = new WebViewTransport(this);
    }
    m_webTransport->setCredentials(cred);

    // Recreate the hidden browser if the site / user agent changed since it
    // was created (e.g. the user just typed new values in Settings).
    if (m_hiddenBrowser && (m_browserSite != s.site || m_browserUA != s.userAgent)) {
        m_webTransport->setBrowser(nullptr);
        m_hiddenBrowser->close();
        delete m_hiddenBrowser;
        m_hiddenBrowser = nullptr;
    }

    if (!m_hiddenBrowser) {
        m_hiddenBrowser = new CookieGrabberDialog(s.site, s.userAgent, nullptr);
        // No parent: hand over this window's WebView2 session folder explicitly.
        m_hiddenBrowser->setProperty(WebViewSession::kProperty,
                                     property(WebViewSession::kProperty));
        m_hiddenBrowser->setAttribute(Qt::WA_DontShowOnScreen);
        m_hiddenBrowser->show();
        m_browserSite = s.site;
        m_browserUA = s.userAgent;

        // Fetch the balance as soon as the page is actually loaded.
        connect(m_hiddenBrowser, &CookieGrabberDialog::pageLoaded, this, [this] {
            if (m_live.enabled && !m_live.isDirect() && m_stake)
                m_stake->fetchBalances();
            else
                requestStakeBackgroundBalance(); // Shuffle / Goated / Wolfbet active: fill Stake's cache
        });
    }

}

void MainWindow::applyGoatedCredentials(const LiveSettings &s)
{
    GoatedCredentials gc;
    gc.token = s.goatedToken;
    m_goated->setCredentials(gc);
}

void MainWindow::applyWolfbetCredentials(const LiveSettings &s)
{
    WolfbetCredentials wc;
    wc.token = s.wolfbetToken;
    m_wolfbet->setCredentials(wc);
}

void MainWindow::applyParadiceCredentials(const LiveSettings &s)
{
    ParadiceCredentials pc;
    pc.token = s.paradiceToken;
    m_paradice->setCredentials(pc);
}

void MainWindow::applyPasinoCredentials(const LiveSettings &s)
{
    PasinoCredentials pc;
    pc.token = s.pasinoToken;
    m_pasino->setCredentials(pc);
}

void MainWindow::applyTowerCredentials(const LiveSettings &s)
{
    TowerCredentials tc;
    tc.token = s.towerToken;
    tc.clearance = s.towerClearance;
    tc.userAgent = s.towerUserAgent;
    m_tower->setCredentials(tc);
}

void MainWindow::applyWinnaCredentials(const LiveSettings &s)
{
    WinnaCredentials wc;
    wc.session = s.winnaSession;
    wc.clearance = s.winnaClearance;
    wc.userAgent = s.winnaUserAgent;
    wc.deviceId = s.winnaDeviceId;
    m_winna->setCredentials(wc);
}

void MainWindow::applyDuelCredentials(const LiveSettings &s)
{
    DuelCredentials dc;
    dc.token = s.duelToken;
    dc.uuid = s.duelUuid;
    dc.clearance = s.duelClearance;
    dc.userAgent = s.duelUserAgent;
    dc.site = s.duelSite;
    m_duel->setCredentials(dc);
    // Extension mode: the requests go out from the Duel tab, no hidden browser is needed.
    if (!s.duelExtension && !m_duel->credentials().token.isEmpty()
        && !m_duel->credentials().uuid.isEmpty())
        ensureDuelBrowser();
    if (m_duelBrowser)
        m_duelBrowser->setSite(s.duelSite); // a new mirror reloads the hidden page there
}

// Duel's Cloudflare drops plain HTTPS from the app, so its requests run in a hidden WebView2
// page on duel.com (same session folder as "Open Duel.com & Log in": it is logged in too).
void MainWindow::ensureDuelBrowser()
{
    if (m_duelBrowser || !DuelBrowser::isAvailable())
        return;
    m_duelBrowser = new DuelBrowser(nullptr);
    // No parent: hand over this window's WebView2 session folder explicitly.
    m_duelBrowser->setProperty(WebViewSession::kProperty, property(WebViewSession::kProperty));
    m_duelBrowser->setAttribute(Qt::WA_DontShowOnScreen);
    m_duelBrowser->show();
    m_duel->setBrowser(m_duelBrowser);
    // Read the balance as soon as the page is there (active casino or background cache).
    connect(m_duelBrowser, &DuelBrowser::pageLoaded, this, [this] {
        if (!m_live.enabled && !m_optionsDialog)
            return;
        if (m_stake->isDuel())
            m_stake->fetchBalances();
        else if (m_duel->isReady())
            m_duel->requestBalances();
    });
}

// Stake's session (extension server or hidden WebView2 browser) is kept up while Goated is the
// active casino, like it already is for Shuffle, so Stake's balance can be read in the
// background and a switch back finds it connected. Only when Stake credentials are set.
void MainWindow::keepStakeSessionAlive(const LiveSettings &s)
{
    if (s.apiKey.isEmpty() && !s.extensionMode)
        return;
    // Extension mode: the server is kept running by updateExtensionServer().
    if (!s.extensionMode) {
        ensureHiddenBrowser(s);
        m_webTransport->setBrowser(m_hiddenBrowser); // so it can read Stake's balance
    }
}

IStakeTransport *MainWindow::stakeSessionTransport() const
{
    if (m_live.extensionMode)
        return m_ext;
    return m_webTransport; // may be null before the hidden browser was created
}

// Reads Stake's balances through its own session while another casino is active. The answer
// only fills the cache (onStakeBackgroundBalances).
void MainWindow::requestStakeBackgroundBalance()
{
    if (!m_live.enabled || m_live.casino == LiveSettings::Casino::Stake)
        return;
    if (m_live.apiKey.isEmpty() && !m_live.extensionMode)
        return;
    if (IStakeTransport *t = stakeSessionTransport())
        m_stake->fetchBalancesVia(t); // does nothing unless the transport is ready
}

void MainWindow::fetchActiveBalance(int minGapMs)
{
    QElapsedTimer &clock = m_activeFetchClock[int(m_live.casino)];
    if (clock.isValid() && clock.elapsed() < minGapMs)
        return;
    clock.start();
    m_stake->fetchBalances();
}

void MainWindow::refreshBackgroundBalances()
{
    if (!m_live.enabled)
        return;
    // At most one round of background reads every 10 s, however often the casino is switched.
    if (m_bgBalanceClock.isValid() && m_bgBalanceClock.elapsed() < 10000)
        return;
    m_bgBalanceClock.start();
    if (!m_live.isShuffle())
        updateShuffleBackground(m_live, false); // reopens a dropped socket, re-reads the balances
    if (!m_live.isGoated() && m_goated->isReady())
        m_goated->requestBalances();
    if (!m_live.isWolfbet() && m_wolfbet->isReady())
        m_wolfbet->requestBalances();
    if (!m_live.isParadice() && m_paradice->isReady())
        m_paradice->requestBalances();
    if (!m_live.isPasino() && m_pasino->isReady())
        m_pasino->requestBalances();
    if (!m_live.isTower() && m_tower->isReady())
        m_tower->requestBalances();
    if (!m_live.isWinna() && m_winna->isReady())
        m_winna->requestBalances();
    if (!m_live.isDuel() && m_duel->isReady())
        m_duel->requestBalances();
    requestStakeBackgroundBalance();
}

void MainWindow::onStakeBackgroundBalances(const QMap<QString, double> &balances)
{
    if (!m_live.enabled || m_live.casino == LiveSettings::Casino::Stake)
        return; // Stake is active: its normal fetch fills the cache
    m_stakeBalances = balances;
    refreshStakeBalanceVar();
}

// The extension server runs while any casino uses the "Browser extension" login method; each
// casino's transport sends its requests through it when that casino's own method is the extension.
bool MainWindow::updateExtensionServer(const LiveSettings &s, QString *errorOut)
{
    StakeCredentials cred;
    cred.site = s.site;
    cred.apiKey = s.apiKey;
    cred.clearance = s.clearance;
    cred.userAgent = s.userAgent;
    m_ext->setCredentials(cred);
    m_ext->setAllowedHosts(s.allExtensionHosts());

    m_shuffle->setExtension(s.shuffleExtension ? m_ext : nullptr);
    m_goated->setExtension(s.goatedExtension ? m_ext : nullptr);
    m_wolfbet->setExtension(s.wolfbetExtension ? m_ext : nullptr);
    m_paradice->setExtension(s.paradiceExtension ? m_ext : nullptr);
    m_pasino->setExtension(s.pasinoExtension ? m_ext : nullptr);
    m_tower->setExtension(s.towerExtension ? m_ext : nullptr);
    m_winna->setExtension(s.winnaExtension ? m_ext : nullptr);
    m_duel->setExtension(s.duelExtension ? m_ext : nullptr);

    if (!s.anyUsesExtension()) {
        m_ext->stop();
        return true;
    }
    QString err;
    if (m_ext->start(s.extensionPort, &err))
        return true;
    const QString msg = tr("Could not start the extension server on 127.0.0.1:%1 (%2)")
                            .arg(s.extensionPort)
                            .arg(err);
    onLogMessage(msg, true);
    if (errorOut)
        *errorOut = msg;
    return false;
}

bool MainWindow::configureConnection(const LiveSettings &s, QString *errorOut)
{
    QString extErr;
    const bool extOk = updateExtensionServer(s, &extErr);
    const bool ok = configureTransports(s, errorOut);
    m_extActiveConnected = s.activeUsesExtension()
                           && (m_ext->isConnectedTo(s.extensionHosts(s.casino))
                               || (s.casino == LiveSettings::Casino::Stake && m_ext->isReady()));
    if (ok && !extOk && s.activeUsesExtension()) {
        if (errorOut)
            *errorOut = extErr;
        return false;
    }
    return ok;
}

bool MainWindow::configureTransports(const LiveSettings &s, QString *errorOut)
{
    if (s.isWinna()) {
        // Direct mode: plain HTTPS straight from the app (session + cf_clearance + its User-Agent).
        // The Stake browser / extension are left as they are for a quick switch back.
        applyWinnaCredentials(s);
        applyTowerCredentials(s);    // so Tower's balance can be read in the background
        applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyGoatedCredentials(s);   // so Goated's balance can be read in the background
        applyWolfbetCredentials(s);  // so Wolfbet's balance can be read in the background
        applyDuelCredentials(s);     // so Duel's balance can be read in the background

        // Shuffle's balance socket keeps running in the background like it does for Stake.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_winna);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isTower()) {
        // Direct mode: plain HTTPS straight from the app (token + cf_clearance + its User-Agent).
        // The Stake browser / extension are left as they are for a quick switch back.
        applyTowerCredentials(s);
        applyWinnaCredentials(s);    // so Winna's balance can be read in the background
        applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyGoatedCredentials(s);   // so Goated's balance can be read in the background
        applyWolfbetCredentials(s);  // so Wolfbet's balance can be read in the background
        applyDuelCredentials(s);     // so Duel's balance can be read in the background

        // Shuffle's balance socket keeps running in the background like it does for Stake.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_tower);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isPasino()) {
        // Direct mode: plain HTTPS straight from the app, only the token is needed. The
        // Stake browser / extension are left as they are for a quick switch back.
        applyPasinoCredentials(s);
        applyTowerCredentials(s);    // so Tower's balance can be read in the background
        applyWinnaCredentials(s);   // so Winna's balance can be read in the background
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyGoatedCredentials(s);   // so Goated's balance can be read in the background
        applyWolfbetCredentials(s);  // so Wolfbet's balance can be read in the background
        applyDuelCredentials(s);     // so Duel's balance can be read in the background

        // Shuffle's balance socket keeps running in the background like it does for Stake.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_pasino);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isParadice()) {
        // Direct mode: plain HTTPS GraphQL straight from the app, only the token is needed. The
        // Stake browser / extension are left as they are for a quick switch back.
        applyParadiceCredentials(s);
        applyPasinoCredentials(s);  // so Pasino's balance can be read in the background
        applyTowerCredentials(s);   // so Tower's balance can be read in the background
        applyWinnaCredentials(s);   // so Winna's balance can be read in the background
        applyGoatedCredentials(s);  // so Goated's balance can be read in the background
        applyWolfbetCredentials(s); // so Wolfbet's balance can be read in the background
        applyDuelCredentials(s);    // so Duel's balance can be read in the background

        // Shuffle's balance socket keeps running in the background like it does for Stake.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_paradice);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isDuel()) {
        // Direct mode: plain HTTPS straight from the app (cookie + UUID; the security token is
        // fetched by the transport). The Stake browser / extension are left as they are.
        applyDuelCredentials(s);
        applyGoatedCredentials(s);  // so Goated's balance can be read in the background
        applyWolfbetCredentials(s); // so Wolfbet's balance can be read in the background
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
        applyTowerCredentials(s);   // so Tower's balance can be read in the background
        applyWinnaCredentials(s);   // so Winna's balance can be read in the background

        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_duel);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isWolfbet()) {
        // Direct mode: plain HTTPS straight from the app, only the token is needed. The
        // Stake browser / extension are left as they are for a quick switch back.
        applyWolfbetCredentials(s);
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
        applyTowerCredentials(s);   // so Tower's balance can be read in the background
        applyWinnaCredentials(s);   // so Winna's balance can be read in the background
        applyGoatedCredentials(s); // so Goated's balance can be read in the background
        applyDuelCredentials(s);   // so Duel's balance can be read in the background

        // Shuffle's balance socket keeps running in the background like it does for Stake.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_wolfbet);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isGoated()) {
        // Direct mode: plain HTTPS straight from the app, only the token is needed. The
        // Stake browser / extension are left as they are for a quick switch back.
        GoatedCredentials gc;
        gc.token = s.goatedToken;
        m_goated->setCredentials(gc);
        applyWolfbetCredentials(s); // so Wolfbet's balance can be read in the background
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
        applyTowerCredentials(s);   // so Tower's balance can be read in the background
        applyWinnaCredentials(s);   // so Winna's balance can be read in the background
        applyDuelCredentials(s);    // so Duel's balance can be read in the background

        // Shuffle's balance socket keeps running in the background like it does for Stake.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
        keepStakeSessionAlive(s);

        m_stake->setTransport(m_goated);
        m_stake->setCurrency(s.currency);
        return true;
    }
    if (s.isShuffle()) {
        // Direct mode: HTTPS + websocket straight from the app. No WebView2, no extension.
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        // Stake extension mode: the server stays up (updateExtensionServer()), so switching back
        // to Stake finds the tab still connected.
        if (!s.extensionMode) {
            ensureHiddenBrowser(s); // keep WebView2 open for a quick switch back to Stake
            m_webTransport->setBrowser(m_hiddenBrowser); // so it can read Stake's balance
        }
        applyGoatedCredentials(s); // so Goated's balance can be read in the background
        applyWolfbetCredentials(s); // so Wolfbet's balance can be read in the background
        applyParadiceCredentials(s); // so Paradice's balance can be read in the background
        applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
        applyTowerCredentials(s);   // so Tower's balance can be read in the background
        applyWinnaCredentials(s);   // so Winna's balance can be read in the background
        applyDuelCredentials(s);    // so Duel's balance can be read in the background
        m_stake->setTransport(m_shuffle);
        m_stake->setCurrency(s.currency);
        updateShuffleBackground(s, false);
        return true;
    }
    // Stake is active: Shuffle's balance socket stays open in the background (when Shuffle
    // credentials are filled in), so its balance is current the moment we switch back.
    {
        ShuffleCredentials sc;
        sc.site = s.shuffleSite;
        sc.token = s.shuffleToken;
        sc.correlationUserId = s.correlationUserId;
        sc.browserId = s.browserId;
        m_shuffle->setCredentials(sc);
        updateShuffleBackground(s, false);
    }
    applyGoatedCredentials(s); // so Goated's balance can be read in the background
    applyWolfbetCredentials(s); // so Wolfbet's balance can be read in the background
    applyParadiceCredentials(s); // so Paradice's balance can be read in the background
    applyPasinoCredentials(s);   // so Pasino's balance can be read in the background
    applyTowerCredentials(s);   // so Tower's balance can be read in the background
    applyWinnaCredentials(s);   // so Winna's balance can be read in the background
    applyDuelCredentials(s);    // so Duel's balance can be read in the background

    StakeCredentials cred;
    cred.site = s.site;
    cred.apiKey = s.apiKey;
    cred.clearance = s.clearance;
    cred.userAgent = s.userAgent;
    m_cookie->setCredentials(cred);
    Q_UNUSED(errorOut); // a failing extension server is reported by configureConnection()

    if (s.extensionMode) {
        m_stake->setTransport(m_ext); // server started by updateExtensionServer()
    } else {
        
        ensureHiddenBrowser(s);
        m_webTransport->setBrowser(m_hiddenBrowser);
        m_stake->setTransport(m_webTransport);
    }
    m_stake->setCurrency(s.currency);
    return true;
}

void MainWindow::applyLiveSettings(const LiveSettings &requested)
{
    LiveSettings next = requested;
    const bool wasEnabled = m_live.enabled;
    const bool casinoChanged = (m_live.casino != next.casino);

    // Remember the balance of the casino we are leaving (Goated / Stake) before m_live changes.
    if (wasEnabled && casinoChanged)
        snapshotLeavingCasinoBalance();
    // Another account or live betting switched off: the cached figures no longer apply.
    if (!next.enabled || m_live.goatedToken != next.goatedToken)
        m_goatedBalances.clear();
    if (!next.enabled || m_live.wolfbetToken != next.wolfbetToken)
        m_wolfbetBalances.clear();
    if (!next.enabled || m_live.paradiceToken != next.paradiceToken)
        m_paradiceBalances.clear();
    if (!next.enabled || m_live.pasinoToken != next.pasinoToken)
        m_pasinoBalances.clear();
    if (!next.enabled || m_live.towerToken != next.towerToken)
        m_towerBalances.clear();
    if (!next.enabled || m_live.winnaSession != next.winnaSession)
        m_winnaBalances.clear();
    if (!next.enabled || m_live.duelToken != next.duelToken || m_live.duelUuid != next.duelUuid
        || m_live.duelSite != next.duelSite)
        m_duelBalances.clear();
    if (!next.enabled || m_live.apiKey != next.apiKey || m_live.site != next.site)
        m_stakeBalances.clear();

    m_live = next;
    m_balances.clear();
	m_isLoggedIn = false;
	
    QString err;
    if (!configureConnection(m_live, &err) && m_live.enabled) {
        // Do NOT set m_live.enabled = false here. 
        // Just log the error so the checkbox stays ticked and the app tries again.
        onLogMessage(tr("Live connection failed to initialize: %1").arg(err), true);
    }

    const bool modeChanged = (wasEnabled != m_live.enabled);
    const bool live = m_live.enabled;

    m_engine->setLiveEnabled(live); // also marks the balance as "not yet fetched"
    if (casinoChanged && live)
        m_engine->invalidateLiveBalance(); // the old casino's balance must not be used for the new one
    refreshShuffleBalanceVar();
    refreshGoatedBalanceVar();
    refreshWolfbetBalanceVar();
    refreshParadiceBalanceVar();
    refreshPasinoBalanceVar();
    refreshTowerBalanceVar();
    refreshWinnaBalanceVar();
    refreshDuelBalanceVar();
    refreshStakeBalanceVar();
    // Switching to Shuffle: its balance has been tracked in the background, so load it into
    // the session stats right now instead of waiting for a new login round trip.
    if (live && m_live.isShuffle())
        loadCachedShuffleBalance();
    // Same for Goated / Stake: show the last known balance at once; the fetch below confirms it.
    else if (live && casinoChanged)
        loadCachedCasinoBalance();
    // Bet sizes always carry 8 decimals and 0 is a valid bet, in both modes.
    m_manualPanel->setBetPrecision(8, 0.0);
    m_autoPanel->setBetPrecision(8, 0.0);

    // Mixing simulated and real bets in one history would be misleading, so only
    // reset when switching between simulation and live - not on every re-login.
    if (modeChanged) {
        resetSessionState();
        m_clientSeedPending = true;
    }
    if (!live)
        m_manualPanel->setStartBalance(m_startBalance);

    m_lastBalanceError.clear();
    if (live) {
        m_engine->markLiveConnecting(); // Start / Roll wait for the login instead of failing
        if (m_live.activeUsesExtension() && !m_optionsDialog
            && m_ext->isConnectedTo(extensionTabHosts(m_live, m_live.casino))) {
            // The login is read from the casino's tab first (it may have changed), then the balances.
            m_extGrabClock.start();
            refreshExtensionLogin(m_live.casino);
        } else {
            fetchActiveBalance(4000); // not again within 4 s: quick script switches would hit rate limits
        }
        // The casino just left is read again right away (exact server figure), the others too.
        refreshBackgroundBalances();
        m_balanceTimer->setInterval(m_isLoggedIn ? 10000 : 2000); // quick retries until logged in
        m_balanceTimer->start();
        onLogMessage(tr("Live betting enabled (%1, %2). Fetching balance…")
                         .arg(m_live.activeSite(),
                              m_live.activeUsesExtension() ? tr("extension")
                                                           : (m_live.isDirect() ? tr("direct") : tr("cookie"))),
                     false);
    } else {
        m_balanceTimer->stop();
        updateShuffleBackground(m_live); // live is off: stops and closes the Shuffle socket
        if (modeChanged)
            onLogMessage(tr("Back to simulation (play money)."), false);
    }
    updateModeWidgets();
}

void MainWindow::onBalancesReady(const QMap<QString, double> &reported)
{
    // Which casino answered: the connection is configured for one casino at a time (the
    // active one, or the one being tested with "Log in" in the Options dialog).
    const LiveSettings::Casino from = m_stake->isShuffle()   ? LiveSettings::Casino::Shuffle
                                      : m_stake->isGoated()  ? LiveSettings::Casino::Goated
                                      : m_stake->isWolfbet() ? LiveSettings::Casino::Wolfbet
                                      : m_stake->isDuel()    ? LiveSettings::Casino::Duel
                                      : m_stake->isParadice() ? LiveSettings::Casino::Paradice
                                      : m_stake->isPasino()  ? LiveSettings::Casino::Pasino
                                      : m_stake->isTower()   ? LiveSettings::Casino::Tower
                                      : m_stake->isWinna()   ? LiveSettings::Casino::Winna
                                                             : LiveSettings::Casino::Stake;
    // The dialog only takes answers for the casino it is showing; the main window only for
    // the active casino (a login test for another casino must not change its balance).
    const bool forDialog = m_optionsDialog && m_optionsDialog->liveSettings().casino == from;
    const bool forActive = m_live.casino == from;

    // Stake / Goated / Wolfbet / Duel / Paradice / Pasino always answer with the full currency list; Shuffle only once
    // its full list was read (before that its websocket may be pushing balances one by one).
    const bool listComplete = from != LiveSettings::Casino::Shuffle || m_shuffle->balanceListKnown();

    // The bet currency is whatever the user typed (per casino). It is never replaced by
    // another one; a typed currency the account does not list holds 0 (0-amount bets work).
    QMap<QString, double> balances = reported;
    const QString dialogCur = forDialog
        ? m_optionsDialog->liveSettings().currency.trimmed().toLower() : QString();
    const QString activeCur = m_live.currency.trimmed().toLower();
    if (listComplete) {
        if (!dialogCur.isEmpty() && !balances.contains(dialogCur))
            balances.insert(dialogCur, 0.0);
        if (forActive && !activeCur.isEmpty() && !balances.contains(activeCur))
            balances.insert(activeCur, 0.0);
    }
    const QStringList codes = reported.keys(); // what the account really has

    if (forDialog) {
        // Hint only (what the account holds); the typed currency is never changed.
        m_optionsDialog->setLiveCurrencies(codes, dialogCur);

        // "Logged in" only with every login field filled in and the currencies loaded.
        const QString missing = m_optionsDialog->missingLoginData();
        if (!missing.isEmpty())
            m_optionsDialog->setLoginStatus(tr("Not logged in — %1").arg(missing), false);
        else if (!listComplete)
            m_optionsDialog->setLoginStatus(tr("Connected — loading the currencies…"), true);
        else if (dialogCur.isEmpty())
            m_optionsDialog->setLoginStatus(
                tr("Logged in — currencies loaded. Type the bet currency (e.g. usdt) and "
                   "press OK."),
                true);
        else
            m_optionsDialog->setLoginStatus(
                tr("Logged in — %1 %2 available")
                    .arg(formatAmount(balances.value(dialogCur), 8), dialogCur.toUpper()),
                true);
    }

    if (!forActive)
        return;

    m_balances = balances;
    if (from == LiveSettings::Casino::Stake && m_live.enabled) {
        m_stakeBalances = balances;
        refreshStakeBalanceVar();
    }
    m_lastBalanceError.clear();
    if (!listComplete) {
        updateConnectionStatus(); // Shuffle: not logged in until its currencies are loaded
        return;
    }
    m_isLoggedIn = true;

    if (!m_live.enabled)
        return;

    if (activeCur.isEmpty()) {
        // No bet currency typed yet: nothing to bet with until one is chosen in Options.
        updateConnectionStatus();
        return;
    }

    const double balance = balances.value(activeCur);
    // Shuffle's websocket reports the new balance right after each bet, usually before the
    // bet's own answer is processed. Re-syncing then would count that bet twice, so while a
    // bet is pending or an auto-bet runs the engine's own running balance is kept.
    const bool shuffle = from == LiveSettings::Casino::Shuffle;
    if (!(shuffle && (m_engine->betInFlight() || m_engine->isRunning())))
        m_engine->syncBalance(balance);
    m_manualPanel->setStartBalance(m_engine->stats().balance);
    updateSessionWidgets();
    updateStatusWidgets();
    updateConnectionStatus();
}

// ------------------------------------------------------- Shuffle balance cache

QString MainWindow::shuffleCurrency() const
{
    return (m_live.isShuffle() ? m_live.currency : m_currencyByCasino[1]).trimmed().toLower();
}

// Recompute m_shuffleBalance (the Shuffle bet currency's balance) from the cache.
void MainWindow::refreshShuffleBalanceVar()
{
    const QString cur = shuffleCurrency();
    // Not in Shuffle's full balance list = zero balance.
    m_shuffleBalanceKnown = !cur.isEmpty()
        && (m_shuffleBalances.contains(cur) || m_shuffle->balanceListKnown());
    m_shuffleBalance = m_shuffleBalanceKnown ? m_shuffleBalances.value(cur) : 0.0;
}

// Every BalanceUpdated push (and the HTTPS read after connecting) lands here, whichever
// casino is active - this does not go through StakeClient.
void MainWindow::onShuffleBalancesUpdated(const QMap<QString, double> &balances)
{
    m_shuffleBalances = balances;
    m_lastShuffleBgError.clear();
    refreshShuffleBalanceVar();
}

// Errors of the background socket while Stake is active (when Shuffle is active they come
// through onBalancesFailed). Logged once per distinct message.
void MainWindow::onShuffleBackgroundError(const QString &error)
{
    if (m_live.isShuffle() || !m_live.enabled)
        return;
    if (error == m_lastShuffleBgError)
        return;
    m_lastShuffleBgError = error;
    onLogMessage(tr("Shuffle balance (background): %1").arg(error), true);
}

// Background socket policy: run while live betting is on and the Shuffle credentials are
// complete, whichever casino is active.
void MainWindow::updateShuffleBackground(const LiveSettings &s, bool closeIfOff)
{
    const bool want = s.enabled && m_shuffle->isReady();
    m_shuffle->setKeepAlive(want);
    if (want)
        m_shuffle->requestBalances(); // opens the socket when it is not open yet
    else if (closeIfOff)
        m_shuffle->closeSocket();
}

// Puts the cached Shuffle balance into the session stats. Only done when the socket is
// logged in and the figure was refreshed since, otherwise the normal fetch path is used.
bool MainWindow::loadCachedShuffleBalance()
{
    const QString cur = m_live.currency.trimmed().toLower();
    if (cur.isEmpty() || !m_shuffle->hasFreshBalance(cur))
        return false;
    if (!m_shuffleBalances.contains(cur) && !m_shuffle->balanceListKnown())
        return false;
    if (!m_shuffleBalances.contains(cur))
        m_shuffleBalances.insert(cur, 0.0); // not in the full list = zero balance

    m_balances = m_shuffleBalances;
    m_isLoggedIn = true;
    m_engine->syncBalance(m_shuffleBalances.value(cur));
    m_manualPanel->setStartBalance(m_engine->stats().balance);
    updateSessionWidgets();
    updateStatusWidgets();
    updateConnectionStatus();
    return true;
}

// ------------------------------------------------- Goated / Stake balance cache

QString MainWindow::goatedCurrency() const
{
    return (m_live.isGoated() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Goated)])
        .trimmed().toLower();
}

QString MainWindow::stakeCurrency() const
{
    return (m_live.casino == LiveSettings::Casino::Stake
                ? m_live.currency
                : m_currencyByCasino[int(LiveSettings::Casino::Stake)])
        .trimmed().toLower();
}

// Recompute m_goatedBalance (the Goated bet currency's balance) from the cache.
void MainWindow::refreshGoatedBalanceVar()
{
    const QString cur = goatedCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_goatedBalanceKnown = !cur.isEmpty()
        && (m_goatedBalances.contains(cur) || !m_goatedBalances.isEmpty());
    m_goatedBalance = m_goatedBalanceKnown ? m_goatedBalances.value(cur) : 0.0;
}

QString MainWindow::wolfbetCurrency() const
{
    return (m_live.isWolfbet() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Wolfbet)])
        .trimmed().toLower();
}

// Recompute m_wolfbetBalance (the Wolfbet bet currency's balance) from the cache.
void MainWindow::refreshWolfbetBalanceVar()
{
    const QString cur = wolfbetCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_wolfbetBalanceKnown = !cur.isEmpty()
        && (m_wolfbetBalances.contains(cur) || !m_wolfbetBalances.isEmpty());
    m_wolfbetBalance = m_wolfbetBalanceKnown ? m_wolfbetBalances.value(cur) : 0.0;
}

QString MainWindow::duelCurrency() const
{
    return (m_live.isDuel() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Duel)])
        .trimmed().toLower();
}

// Recompute m_duelBalance (the Duel bet currency's balance) from the cache.
void MainWindow::refreshDuelBalanceVar()
{
    const QString cur = duelCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_duelBalanceKnown = !cur.isEmpty()
        && (m_duelBalances.contains(cur) || !m_duelBalances.isEmpty());
    m_duelBalance = m_duelBalanceKnown ? m_duelBalances.value(cur) : 0.0;
}

// Every Duel balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onDuelBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled || m_duel->credentials().uuid != DuelTransport::cleanUuid(m_live.duelUuid))
        return;
    m_duelBalances = balances;
    refreshDuelBalanceVar();
}

QString MainWindow::paradiceCurrency() const
{
    return (m_live.isParadice() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Paradice)])
        .trimmed().toLower();
}

// Recompute m_paradiceBalance (the Paradice bet currency's balance) from the cache.
void MainWindow::refreshParadiceBalanceVar()
{
    const QString cur = paradiceCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_paradiceBalanceKnown = !cur.isEmpty()
        && (m_paradiceBalances.contains(cur) || !m_paradiceBalances.isEmpty());
    m_paradiceBalance = m_paradiceBalanceKnown ? m_paradiceBalances.value(cur) : 0.0;
}

// Every Paradice balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onParadiceBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled
        || m_paradice->credentials().token != ParadiceTransport::cleanToken(m_live.paradiceToken))
        return;
    m_paradiceBalances = balances;
    refreshParadiceBalanceVar();
}

QString MainWindow::pasinoCurrency() const
{
    return (m_live.isPasino() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Pasino)])
        .trimmed().toLower();
}

// Recompute m_pasinoBalance (the Pasino bet currency's balance) from the cache.
void MainWindow::refreshPasinoBalanceVar()
{
    const QString cur = pasinoCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_pasinoBalanceKnown = !cur.isEmpty()
        && (m_pasinoBalances.contains(cur) || !m_pasinoBalances.isEmpty());
    m_pasinoBalance = m_pasinoBalanceKnown ? m_pasinoBalances.value(cur) : 0.0;
}

// Every Pasino balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onPasinoBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled
        || m_pasino->credentials().token != PasinoTransport::cleanToken(m_live.pasinoToken))
        return;
    m_pasinoBalances = balances;
    refreshPasinoBalanceVar();
}

QString MainWindow::towerCurrency() const
{
    return (m_live.isTower() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Tower)])
        .trimmed().toLower();
}

// Recompute m_towerBalance (the Tower bet currency's balance) from the cache.
void MainWindow::refreshTowerBalanceVar()
{
    const QString cur = towerCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_towerBalanceKnown = !cur.isEmpty()
        && (m_towerBalances.contains(cur) || !m_towerBalances.isEmpty());
    m_towerBalance = m_towerBalanceKnown ? m_towerBalances.value(cur) : 0.0;
}

// Every Tower balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onTowerBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled
        || m_tower->credentials().token != TowerTransport::cleanToken(m_live.towerToken))
        return;
    m_towerBalances = balances;
    refreshTowerBalanceVar();
}

QString MainWindow::winnaCurrency() const
{
    return (m_live.isWinna() ? m_live.currency : m_currencyByCasino[int(LiveSettings::Casino::Winna)])
        .trimmed().toLower();
}

// Recompute m_winnaBalance (the Winna bet currency's balance) from the cache.
void MainWindow::refreshWinnaBalanceVar()
{
    const QString cur = winnaCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_winnaBalanceKnown = !cur.isEmpty()
        && (m_winnaBalances.contains(cur) || !m_winnaBalances.isEmpty());
    m_winnaBalance = m_winnaBalanceKnown ? m_winnaBalances.value(cur) : 0.0;
}

// Every Winna balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onWinnaBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled
        || m_winna->credentials().session != WinnaTransport::cleanSession(m_live.winnaSession))
        return;
    m_winnaBalances = balances;
    refreshWinnaBalanceVar();
}

// Every Wolfbet balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onWolfbetBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled || m_wolfbet->credentials().token != WolfbetTransport::cleanToken(m_live.wolfbetToken))
        return;
    m_wolfbetBalances = balances;
    refreshWolfbetBalanceVar();
    refreshParadiceBalanceVar();
}

// Recompute m_stakeBalance (the Stake bet currency's balance) from the cache.
void MainWindow::refreshStakeBalanceVar()
{
    const QString cur = stakeCurrency();
    // A read happened but the currency is not in it: zero balance.
    m_stakeBalanceKnown = !cur.isEmpty()
        && (m_stakeBalances.contains(cur) || !m_stakeBalances.isEmpty());
    m_stakeBalance = m_stakeBalanceKnown ? m_stakeBalances.value(cur) : 0.0;
}

// Every Goated balance read (the active casino's fetches and the background reads while
// another casino is active) lands here.
void MainWindow::onGoatedBalancesUpdated(const QMap<QString, double> &balances)
{
    // Ignore a read made with other credentials (e.g. a login test in the Options dialog).
    if (!m_live.enabled || m_goated->credentials().token != GoatedTransport::cleanToken(m_live.goatedToken))
        return;
    m_goatedBalances = balances;
    refreshGoatedBalanceVar();
}

// Called right before the casino changes: the engine's running balance is the freshest figure
// there is for the casino being left (bets placed since the last 10 s refresh are included).
// Shuffle is skipped, its websocket keeps the cache current by itself.
void MainWindow::snapshotLeavingCasinoBalance()
{
    if (!m_engine->liveMode() || !m_engine->hasLiveBalance() || m_live.isShuffle())
        return;
    const QString cur = m_live.currency.trimmed().toLower();
    if (cur.isEmpty())
        return;
    const double balance = m_engine->stats().balance;
    if (m_live.isGoated())
        m_goatedBalances.insert(cur, balance);
    else if (m_live.isWolfbet())
        m_wolfbetBalances.insert(cur, balance);
    else if (m_live.isDuel())
        m_duelBalances.insert(cur, balance);
    else if (m_live.isParadice())
        m_paradiceBalances.insert(cur, balance);
    else if (m_live.isPasino())
        m_pasinoBalances.insert(cur, balance);
    else if (m_live.isTower())
        m_towerBalances.insert(cur, balance);
    else if (m_live.isWinna())
        m_winnaBalances.insert(cur, balance);
    else
        m_stakeBalances.insert(cur, balance);
}

// Puts the cached balance of the (new) active casino, Goated or Stake, into the session stats
// so the sidebar and status bar show it immediately and betting can start at once (the cached
// figure is the engine's own balance from when the casino was left, or Goated's background
// read). The fetch already requested confirms or corrects it a moment later.
bool MainWindow::loadCachedCasinoBalance()
{
    if (m_live.isShuffle())
        return false;
    const QMap<QString, double> &cache = m_live.isGoated()    ? m_goatedBalances
                                         : m_live.isWolfbet() ? m_wolfbetBalances
                                         : m_live.isDuel()    ? m_duelBalances
                                         : m_live.isParadice() ? m_paradiceBalances
                                         : m_live.isPasino()  ? m_pasinoBalances
                                         : m_live.isTower()   ? m_towerBalances
                                         : m_live.isWinna()   ? m_winnaBalances
                                                              : m_stakeBalances;
    const QString cur = m_live.currency.trimmed().toLower();
    // Nothing read yet = unknown; read, but the currency is not in it = zero balance.
    if (cur.isEmpty() || cache.isEmpty())
        return false;

    m_balances = cache;
    if (!m_balances.contains(cur))
        m_balances.insert(cur, 0.0);
    m_engine->syncBalance(m_balances.value(cur));
    // Credentials are set and the account was logged in before: stay logged in.
    if (m_stake->transport() && m_stake->transport()->isReady())
        m_isLoggedIn = true;
    m_manualPanel->setStartBalance(m_engine->stats().balance);
    updateSessionWidgets();
    updateStatusWidgets();
    updateConnectionStatus();
    return true;
}

void MainWindow::onBalancesFailed(const QString &error)
{
    if (m_optionsDialog)
        m_optionsDialog->setLoginStatus(error, false);
        
    // ADD THIS BLOCK:
    // Check if the error indicates an expired or invalid login session
    if (error.contains(QLatin1String("invalid"), Qt::CaseInsensitive) ||
        error.contains(QLatin1String("login"), Qt::CaseInsensitive) ||
        error.contains(QLatin1String("unauthorized"), Qt::CaseInsensitive) ||
        error.contains(QLatin1String("401"), Qt::CaseInsensitive)) {
        m_isLoggedIn = false;
        updateConnectionStatus(); 
    }

    if (!m_live.enabled)
        return;
    // Rate limit (HTTP 429): a later refresh succeeds, nothing is wrong with the login.
    if (error.contains(QLatin1String("too many requests"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("429"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("too many attempts"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("rate limit"), Qt::CaseInsensitive))
        return;
    // The hidden browser is still loading: the page-loaded handler / retry timer tries again.
    if (error.contains(QLatin1String("still loading"), Qt::CaseInsensitive)
        || error.contains(QLatin1String("not initialized"), Qt::CaseInsensitive)) {
        updateConnectionStatus();
        return;
    }
    if (error != m_lastBalanceError) {
        m_lastBalanceError = error;
        onLogMessage(tr("Balance refresh failed: %1").arg(error), true);
    }
	updateConnectionStatus();
}

// A casino tab with the extension connected or closed.
void MainWindow::onExtensionReadyChanged(bool ready)
{
    if (ready && m_live.enabled) {
        // Casinos that are not active read their login from their tab and fill their caches.
        for (int i = 0; i < LiveSettings::kCasinoCount; ++i) {
            const auto c = static_cast<LiveSettings::Casino>(i);
            if (c != m_live.casino)
                refreshExtensionLogin(c);
        }
        requestStakeBackgroundBalance();
    }
    if (!m_stake || !m_live.activeUsesExtension()) {
        m_extActiveConnected = false;
        return;
    }
    const bool now = m_ext->isConnectedTo(m_live.extensionHosts(m_live.casino))
                     || (m_live.casino == LiveSettings::Casino::Stake && m_ext->isReady());
    if (now == m_extActiveConnected)
        return; // a tab of another casino came or went
    m_extActiveConnected = now;
    if (now) {
        onLogMessage(tr("Browser extension connected (%1 tab).").arg(m_live.activeSite()), false);
        // The login (API key / token / ids) is read from the tab, then the balances.
        if (m_live.enabled && !m_optionsDialog)
            refreshExtensionLogin(m_live.casino);
    } else {
        onLogMessage(tr("Browser extension: the %1 tab disconnected.").arg(m_live.activeSite()), true);
        m_isLoggedIn = false;
    }
    updateModeWidgets();
}

void MainWindow::onBalanceTimer()
{
    if (!m_live.enabled)
        return;
    // Extension mode and not logged in yet (e.g. logged in in the browser just now): read the
    // login from the tab again, at most every 5 s.
    if (!m_isLoggedIn && m_live.activeUsesExtension() && !m_optionsDialog
        && (!m_extGrabClock.isValid() || m_extGrabClock.elapsed() > 5000)) {
        m_extGrabClock.start();
        refreshExtensionLogin(m_live.casino); // fetches the balances when it is read
        return;
    }
    // Options is open and the active casino has no usable login yet (e.g. the token is still
    // being read from the extension tab): the dialog handles the login, so do not retry here -
    // the retry's "token is empty" would overwrite the dialog's status.
    if (m_optionsDialog && m_stake->transport() && !m_stake->transport()->isReady())
        return;
    fetchActiveBalance(1500);
    // Until the login went through, retry every 2 s so betting can start as early as possible.
    m_balanceTimer->setInterval(m_isLoggedIn ? 10000 : 2000);
    // Every casino that is not active is read in the background (Shuffle socket, Goated HTTPS,
    // Stake extension / hidden browser): the answers only fill the caches, so the balance is
    // current when switching casino, including from a script (throttled to every 10 s).
    refreshBackgroundBalances();
}

double MainWindow::displayBalance() const
{
    const SessionStats &s = m_engine->stats();
    if (m_engine->liveMode())
        return s.balance;
    // While a run is active the session is initialised (resetstats() can leave it at 0 bets).
    return (s.bets > 0 || m_engine->isRunning()) ? s.balance : m_startBalance;
}

bool MainWindow::switchCasinoFromScript(const QString &casino, QString &error)
{
    const LiveSettings::Casino want = LiveSettings::fromName(casino);
    if (want == m_live.casino)
        return true;
    if (m_engine->betInFlight()) {
        error = tr("a bet is still in flight");
        return false;
    }
    // Each casino remembers its own bet currency.
    m_currencyByCasino[int(m_live.casino)] = m_live.currency;

    LiveSettings next = m_live;
    next.casino = want;
    next.currency = m_currencyByCasino[int(want)];
    applyLiveSettings(next); // same live / simulation mode: no session reset
    updateCasinoMenu();
    return true;
}

bool MainWindow::switchCurrencyFromScript(const QString &currency, double &balance, QString &error)
{
    const QString cur = currency.trimmed().toLower();
    if (!m_engine->liveMode()) {
        // Simulation = play money: the currency is only a label.
        m_currencyTag = cur.toUpper();
        updateSessionWidgets();
        updateStatusWidgets();
        return true;
    }
    if (!m_balances.contains(cur)) {
        error = m_balances.isEmpty()
            ? tr("no balances loaded yet")
            : tr("this account has no \"%1\" balance (available: %2)")
                  .arg(cur, m_balances.keys().join(QStringLiteral(", ")));
        return false;
    }
    m_live.currency = cur;
    m_stake->setCurrency(cur);
    refreshShuffleBalanceVar();
    refreshGoatedBalanceVar();
    refreshWolfbetBalanceVar();
    refreshParadiceBalanceVar();
    refreshPasinoBalanceVar();
    refreshTowerBalanceVar();
    refreshWinnaBalanceVar();
    refreshDuelBalanceVar();
    refreshStakeBalanceVar();
    balance = m_balances.value(cur);
    m_stake->fetchBalances(); // refresh the figure for the new currency
    return true;
}

QString MainWindow::currencyLabel() const
{
    if (m_engine->liveMode() && !m_live.currency.isEmpty())
        return m_live.currency.toUpper();
    return m_currencyTag;
}

void MainWindow::updateModeWidgets()
{
    const bool live = m_engine->liveMode();

    // Bet IDs only exist for real bets, so the column is hidden in simulation.
    m_betTable->setColumnHidden(BetHistoryModel::ColBetId, !live);
    if (live) {
        QString why;
        const bool ready = m_stake->transport() && m_stake->transport()->isReady(&why);
        m_statusMode->setText(tr("%1 · %2%3")
                                  .arg(m_live.activeSite(),
                                       m_live.activeUsesExtension()
                                           ? tr("extension")
                                           : (m_live.isDirect() ? tr("direct") : tr("cookie")),
                                       (ready || !m_live.activeUsesExtension())
                                           ? QString() : tr(" (not connected)")));
        m_statusMode->setStyleSheet(QStringLiteral("color:#c9372a;font-weight:bold;"));
    } else {
        m_statusMode->setText(tr("Simulation"));
        m_statusMode->setStyleSheet(QStringLiteral("color:#737373;"));
    }

    updateFairTab();
    m_actRotate->setEnabled(!m_rotatingSeed);
	updateConnectionStatus(); 
    updateWindowTitle();
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this, tr("About xBot"),
        tr("<h3>xBot 1.0</h3>"
           "<p>A play-money dice simulator with a qBittorrent-style interface: "
           "provably fair HMAC-SHA256 rolls, manual and auto-bet with "
           "configurable win/loss progressions, profit chart and session "
           "statistics.</p>"
           "<p>Simulation mode never places real bets; live betting must be "
           "enabled explicitly in Options.</p>"
		   "source code and exe is here: <a href='https://bitbucket.org/poky1084/xbot'>"
           "https://bitbucket.org/poky1084/xbot</a>"
           "<p>Icons are taken from the qBittorrent project "
           "(<a href='https://github.com/qbittorrent/qBittorrent'>"
           "github.com/qbittorrent/qBittorrent</a>, GPLv2).</p>"));
}

// ---------------------------------------------------------------- helpers

BotConfig MainWindow::currentConfig() const
{
    BotConfig cfg = m_manualPanel->config();
    cfg.game = m_game;
    cfg.startBalance = m_startBalance;
    m_autoPanel->applyTo(cfg);
    return cfg;
}

BotConfig MainWindow::manualConfig() const
{
    BotConfig cfg = m_manualPanel->config(); // amount, dice target + side, limbo target
    cfg.game = m_game;
    cfg.startBalance = m_startBalance;
    return cfg;
}

void MainWindow::updateSessionWidgets()
{
    const SessionStats &s = m_engine->stats();
    const bool hasSession = s.bets > 0;
    const double balance = displayBalance();

    // Everything below is relative to the balance the session started with.
    const double base = s.startBalance;
    auto pctOf = [base](double v) -> QString {
        if (base <= 0.0)
            return QStringLiteral("—");
        const double pct = 100.0 * v / base;
        return QLatin1Char('(') + (pct >= 0.0 ? QStringLiteral("+") : QString()) + QString::number(pct, 'f', 1) + QStringLiteral("%)");
    };
    auto timesOf = [base](double v) -> QString {
        if (base <= 0.0)
            return QStringLiteral("—");
        return QLatin1Char('(') + QString::number(v / base, 'f', 2) + QStringLiteral("x)");
    };

    m_lblBalance->setText(formatAmount(balance));
    m_lblProfit->setText(pctOf(s.profit) + QLatin1Char(' ') + formatSigned(s.profit));
    m_lblProfit->setStyleSheet(s.profit >= 0.0 ? QStringLiteral("color:#2e9e46;")
                                               : QStringLiteral("color:#c9372a;"));
    m_lblWagered->setText(timesOf(s.wagered) + QLatin1Char(' ') + formatAmount(s.wagered));
    m_lblWinRate->setText(hasSession ? QStringLiteral("%1%").arg(s.winRate(), 0, 'f', 1)
                                     : QStringLiteral("—"));

    const QString green = QStringLiteral("color:#2e9e46;");
    const QString red = QStringLiteral("color:#c9372a;");
    const QString neutral = QStringLiteral("");

    m_lblHighestBet->setText(hasSession ? formatAmount(s.highestBet) : QStringLiteral("—"));

    m_lblHighestProfit->setText(hasSession ? pctOf(s.highestProfit) + QLatin1Char(' ') + formatSigned(s.highestProfit)
                                           : QStringLiteral("—"));
    m_lblHighestProfit->setStyleSheet(hasSession && s.highestProfit > 0.0 ? green : neutral);

    m_lblLowestProfit->setText(hasSession ? pctOf(s.lowestProfit) + QLatin1Char(' ') + formatSigned(s.lowestProfit)
                                          : QStringLiteral("—"));
    m_lblLowestProfit->setStyleSheet(hasSession && s.lowestProfit < 0.0 ? red : neutral);

    m_lblHighestStreak->setText(hasSession ? QStringLiteral("%1").arg(s.highestStreak)
                                           : QStringLiteral("—"));
    m_lblHighestStreak->setStyleSheet(hasSession && s.highestStreak > 0 ? green : neutral);

    m_lblLowestStreak->setText(hasSession ? QStringLiteral("%1").arg(s.lowestStreak)
                                          : QStringLiteral("—"));
    m_lblLowestStreak->setStyleSheet(hasSession && s.lowestStreak < 0 ? red : neutral);

    if (hasSession && s.currentStreak != 0) {
        m_lblCurrentStreak->setText(s.currentStreak > 0 ? QStringLiteral("+%1").arg(s.currentStreak)
                                                        : QStringLiteral("%1").arg(s.currentStreak));
        m_lblCurrentStreak->setStyleSheet(s.currentStreak > 0 ? green : red);
    } else {
        m_lblCurrentStreak->setText(QStringLiteral("—"));
        m_lblCurrentStreak->setStyleSheet(neutral);
    }

    // A value may overlap its label, but its font is shrunk if it would still be wider than
    // the whole row (the sidebar can be resized, so its current width is used).
    auto fitValue = [this](QLabel *lbl) {
        QFont f = lbl->font();
        f.setPointSizeF(8.5);
        const int rowW = std::max(m_sidebar->width(), m_sidebar->minimumWidth()) - 16 /*grid margins*/ - 2 /*slack*/;
        while (f.pointSizeF() > 6.0 && QFontMetrics(f).horizontalAdvance(lbl->text()) > rowW)
            f.setPointSizeF(f.pointSizeF() - 0.25);
        lbl->setFont(f);
    };
    fitValue(m_lblBalance);
    fitValue(m_lblProfit);
    fitValue(m_lblWagered);
    fitValue(m_lblHighestBet);
    fitValue(m_lblHighestProfit);
    fitValue(m_lblLowestProfit);
}

void MainWindow::updateRunTime()
{
    qint64 ms = m_runAccumMs;
    if (m_runElapsed.isValid())
        ms += m_runElapsed.elapsed();
    const qint64 totalSec = ms / 1000;
    const qint64 h = totalSec / 3600;
    const qint64 m = (totalSec % 3600) / 60;
    const qint64 sec = totalSec % 60;
    m_lblRunTime->setText(QStringLiteral("%1:%2:%3")
                              .arg(h, 2, 10, QLatin1Char('0'))
                              .arg(m, 2, 10, QLatin1Char('0'))
                              .arg(sec, 2, 10, QLatin1Char('0')));
}

void MainWindow::startRunClock()
{
    if (!m_runElapsed.isValid())
        m_runElapsed.start();
    m_runClock->start();
    updateRunTime();
}

void MainWindow::stopRunClock()
{
    if (m_runElapsed.isValid()) {
        m_runAccumMs += m_runElapsed.elapsed();
        m_runElapsed.invalidate();
    }
    m_runClock->stop();
    updateRunTime();
}

void MainWindow::updateStatusWidgets()
{
    const SessionStats &s = m_engine->stats();
    const double balance = displayBalance();

    m_statusBets->setText(tr("Bets: %1").arg(s.bets));
    m_statusWagered->setText(tr("Wagered: %1").arg(formatAmount(s.wagered)));
    m_statusPl->setText(tr("P/L: %1").arg(formatSigned(s.profit)));
    m_statusPl->setStyleSheet(s.profit >= 0.0 ? QStringLiteral("color:#2e9e46;")
                                              : QStringLiteral("color:#c9372a;"));

    // Betting speed over a sliding window of roughly a second.
    const qint64 elapsedMs = m_speedTimer.isValid() ? m_speedTimer.elapsed() : 0;
    if (elapsedMs >= 1000) {
        m_betsPerSecond = double(s.bets - m_speedBetsAtWindowStart) * 1000.0 / double(elapsedMs);
        m_speedBetsAtWindowStart = s.bets;
        m_speedTimer.restart();
    }
    if (!m_engine->isRunning())
        m_betsPerSecond = 0.0;
    m_statusSpeed->setText(QStringLiteral("%1/s").arg(m_betsPerSecond, 0, 'f', 1));
    m_statusBalance->setText(QStringLiteral("%1 %2")
                                 .arg(formatAmount(balance),
                                      currencyLabel()));
}

void MainWindow::updateFilterCounts()
{
    const SessionStats &s = m_engine->stats();
    const QList<QPushButton *> buttons = m_sidebar->findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
        if (!btn->property("filterRow").isValid())
            continue;
        const QString base = btn->property("baseLabel").toString();
        qint64 count = 0;
        switch (btn->property("filterRow").toInt()) {
        case 0: count = s.bets; break;
        case 1: count = s.wins; break;
        case 2: count = s.losses; break;
        case 3: count = s.bigWins; break;
        }
        btn->setText(QStringLiteral("%1 (%2)").arg(base).arg(count));
    }
}

void MainWindow::updateChartLabels()
{
    const SessionStats &s = m_engine->stats();
    m_chartMax->setText(tr("Max: %1").arg(formatSigned(s.highestProfit)));
	m_chartMin->setText(tr("Min: %1").arg(formatSigned(s.lowestProfit)));
    m_chartNow->setText(tr("Now: %1").arg(formatSigned(s.profit)));
    m_chartNow->setStyleSheet(s.profit >= 0.0 ? QStringLiteral("color:#2e9e46;")
                                              : QStringLiteral("color:#c9372a;"));
}

void MainWindow::updateEmptyState()
{
    const bool empty = m_model->rowCount() == 0;
    m_emptyLabel->setVisible(empty);
    if (empty) {
        m_emptyLabel->setGeometry(m_betTable->viewport()->rect());
        m_emptyLabel->raise();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_centralArea && event->type() == QEvent::Resize) {
        positionBottomPanel();
        if (!m_separatorDrag) // e.g. window resized, a panel moved: top panel fills down
            QTimer::singleShot(0, this, [this] { fitTopDock(false); });
    }
    // Mouse on the main window itself = a dock separator being dragged: the Log's height is
    // the user's choice then; remember it when the mouse is released.
    if (watched == this) {
        if (event->type() == QEvent::MouseButtonPress) {
            m_separatorDrag = true;
            // Pressed on the separator above the bottom panel (between the panel docked at the
            // top and the bottom panel): the bottom panel must not be dragged taller than its
            // content - cap it for the drag (Qt stops the separator at the cap).
            QWidget *central = centralWidget();
            const auto *me = static_cast<QMouseEvent *>(event);
            if (central && m_panelHeight > 0
                && me->position().toPoint().y() <= central->geometry().top()) {
                central->setMaximumHeight(std::max(central->height(), m_panelHeight));
                m_dragCapped = true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_separatorDrag = false;
            if (m_dragCapped) {
                m_dragCapped = false;
                if (QWidget *central = centralWidget())
                    central->setMaximumHeight(QWIDGETSIZE_MAX); // the cap is for the drag only
            }
            QTimer::singleShot(0, this, [this] {
                rememberLogHeight();
                fitTopDock(false); // no empty space above the panel (a smaller panel stays)
            });
        }
    }
    // A floating panel shown again (View menu, restored layout): Qt gives it back its
    // owner on show, so make it independent of the main window again.
    if (event->type() == QEvent::Show) {
        if (auto *dock = qobject_cast<QDockWidget *>(watched); dock && dock->isFloating())
            detachFloatingDock(dock);
    }
    // Sidebar resized (it is a dock now): re-fit the session value fonts to its width.
    if (m_sidebar && watched == m_sidebar && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this] { updateSessionWidgets(); });
    // Keep every tab page at its natural height. Without a pinned minimum the
    // QTabWidget squeezes the page when the Log dock takes space, and the
    // controls overlap. With it, the page keeps its size and is clipped.
    if (m_tabs && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show)) {
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && m_tabs->indexOf(w) >= 0) {
            if (QLayout *l = w->layout()) {
                const int h = qMax(l->minimumSize().height(), l->sizeHint().height());
                if (w->minimumHeight() != h) {
                    w->setMinimumHeight(h);
                    if (w == m_tabs->currentWidget()) // content grew / shrank: re-fit
                        QTimer::singleShot(0, this, &MainWindow::updateCentralHeight);
                }
            }
        }
    }
    // Bet list: user interaction (wheel / click) stops following the newest bet.
    if (m_betTable
        && (watched == m_betTable->viewport() || watched == m_betTable->verticalScrollBar())) {
        const QEvent::Type t = event->type();
        if (t == QEvent::Wheel) {
            m_betFollow = false;
            QTimer::singleShot(0, this, [this] {
                if (m_betTable->verticalScrollBar()->value() == 0)
                    m_betFollow = true; // wheeled back to the top: resume
            });
        } else if (t == QEvent::MouseButtonPress) {
            m_betFollow = false;
            // Remember whether the row being clicked is already highlighted, so
            // the click handler can toggle the highlight off again.
            if (watched == m_betTable->viewport()) {
                auto *me = static_cast<QMouseEvent *>(event);
                const QModelIndex idx = m_betTable->indexAt(me->pos());
                m_betClickWasSelected = me->button() == Qt::LeftButton && idx.isValid()
                                        && m_betTable->selectionModel()->isRowSelected(idx.row(), {});
            }
        }
    }
    if (watched == m_betTable->viewport() && event->type() == QEvent::Resize
        && m_emptyLabel->isVisibleTo(m_betTable)) {
        m_emptyLabel->setGeometry(m_betTable->viewport()->rect());
        m_emptyLabel->raise();
    }
    return QMainWindow::eventFilter(watched, event);
}

// Draws a small down-arrow PNG for combo boxes and returns a stylesheet snippet
// that uses it (a styled ::drop-down needs an explicit arrow image to show one).
static QString comboArrowRules(const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = dir + QStringLiteral("/dice-ui-combo-arrow-%1.png").arg(color.name().mid(1));
    if (!QFile::exists(path)) {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF({QPointF(4, 7), QPointF(16, 7), QPointF(10, 14)}));
        p.end();
        pm.save(path, "PNG");
    }
    return QStringLiteral(
               "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right;"
               " border: none; width: 18px; }\n"
               "QComboBox::down-arrow { image: url(%1); width: 10px; height: 10px; }\n")
        .arg(path);
}

void MainWindow::applyTheme(bool dark)
{
    QString sheet = QString::fromUtf8(dark ? kDarkStylesheet : kLightStylesheet);
    if (!dark)
        sheet += comboArrowRules(QColor(0x40, 0x40, 0x40));
    // Applied to this window only (cascades to its children and to dialogs parented
    // to it), so each window keeps its own theme. An app-wide sheet would restyle
    // every open window at once.
    if (!qApp->styleSheet().isEmpty())
        qApp->setStyleSheet(QString());
    setStyleSheet(sheet);
    if (m_model)
        m_model->setStripeColor(dark ? QColor(0x29, 0x2d, 0x31) : QColor(0xf6, 0xf8, 0xfb));
    if (m_luaDock)
        m_luaDock->setDark(dark);
}

void MainWindow::loadSettings()
{
    loadConditions();
    ScopedSettings settings(settingsGroup());

    // No saved window state = first launch: Log and Lua docks start open.
    const bool firstLaunch = !settings.contains(QStringLiteral("MainWindow/state"));

    restoreGeometry(settings.value(QStringLiteral("MainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("MainWindow/state")).toByteArray());
    if (!settings.contains(QStringLiteral("MainWindow/sidebarDockSaved"))) {
        // Settings from before the sidebar became a dock: the saved window state does not
        // know it, so put it back on the left explicitly.
        addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
        m_sidebarDock->show();
        resizeDocks({m_sidebarDock}, {210}, Qt::Horizontal);
    }
    {
        // Last opened bottom tab. Signals are blocked so the saved panel height is kept
        // (onTabChanged would re-fit the panel); the tab is the one that was open with it.
        const int tab = settings.value(QStringLiteral("MainWindow/bottomTab"), 0).toInt();
        if (tab >= 0 && tab < m_tabs->count()) {
            const QSignalBlocker blocker(m_tabs);
            m_tabs->setCurrentIndex(tab);
        }
    }
    m_betTable->horizontalHeader()->restoreState(
        settings.value(QStringLiteral("MainWindow/tableState2")).toByteArray());

    // Panel visibility (View menu). Applied after the splitter state is
    // restored; setChecked() fires toggled() and shows/hides the panel.
    m_actShowSidebar->setChecked(settings.value(QStringLiteral("View/sidebar"), true).toBool());
    m_actShowBetList->setChecked(settings.value(QStringLiteral("View/betList"), true).toBool());
    m_actShowTabs->setChecked(settings.value(QStringLiteral("View/bottomPanel"), true).toBool());
    m_betListDock->setVisible(m_actShowBetList->isChecked());
    // Fit the bottom panel to its tab once the window is shown.
    QTimer::singleShot(0, this, [this] { onTabChanged(m_tabs->currentIndex()); });
    if (firstLaunch && m_logDock)
        m_logDock->show();
    {
        // The log dock's visibility comes from restoreState(); sync the menu.
        const QSignalBlocker blocker(m_actShowLog);
        m_actShowLog->setChecked(m_logDock && !m_logDock->isHidden());
    }
    // The Log height to keep (see keepLogHeight()); measured once shown when not saved yet.
    m_logHeight = settings.value(QStringLiteral("MainWindow/logHeight"), -1).toInt();
    if (m_logHeight <= 0)
        QTimer::singleShot(200, this, &MainWindow::rememberLogHeight);
    else
        scheduleKeepLogHeight();
    m_luaDock->loadSettings();
    if (firstLaunch)
        m_luaDock->show();
    {
        const QSignalBlocker blocker(m_actLua);
        m_actLua->setChecked(!m_luaDock->isHidden());
    }

    m_currencyTag = settings.value(QStringLiteral("General/currency"), m_currencyTag).toString();
    m_startBalance = settings.value(QStringLiteral("General/startBalance"), m_startBalance).toDouble();
    m_manualPanel->setStartBalance(m_startBalance);
    m_manualPanel->setAmount(settings.value(QStringLiteral("Manual/betAmount"), 0.0).toDouble());
    m_manualPanel->setTargets(
        settings.value(QStringLiteral("Manual/diceTarget"), m_manualPanel->target()).toDouble(),
        settings.value(QStringLiteral("Manual/diceUnder"),
                       m_manualPanel->side() == BetSide::Under).toBool(),
        settings.value(QStringLiteral("Manual/limboTarget"), m_manualPanel->limboTarget()).toDouble());
    m_manualTargetsLoaded = true;
    m_clientSeed = settings.value(QStringLiteral("General/clientSeed")).toString();
    m_randomClientSeed = settings.value(QStringLiteral("General/randomClientSeed"), false).toBool();
    m_betDelayMs = qMax(0, settings.value(QStringLiteral("General/betDelayMs"), 0).toInt());
    m_engine->setBetDelayMs(m_betDelayMs);
    m_retryNoBalance = settings.value(QStringLiteral("General/retryNoBalance"), false).toBool();
    m_engine->setRetryNoBalance(m_retryNoBalance);
    setGame(settings.value(QStringLiteral("General/game"), QStringLiteral("dice")).toString()
                    == QLatin1String("limbo")
                ? Game::Limbo
                : Game::Dice);

    // Live connection settings. `enabled` is deliberately never restored: every
    // launch starts in simulation and live betting must be switched on again.
    settings.beginGroup(QStringLiteral("Live"));
    // Restore the casino that was active when the app was last closed; on a first launch
    // (nothing saved yet) fall back to the default in AppConfig::casino (bot/LiveSettings.h).
    m_live.casino = LiveSettings::fromName(
        settings.value(QStringLiteral("casino"), AppConfig::casino).toString());
    m_live.site = settings.value(QStringLiteral("site"), m_live.site).toString();
    m_live.shuffleSite = settings.value(QStringLiteral("shuffleSite"), m_live.shuffleSite).toString();
    m_live.extensionMode = settings.value(QStringLiteral("extensionMode"), false).toBool();
    m_live.shuffleExtension = settings.value(QStringLiteral("shuffleExtension"), false).toBool();
    m_live.goatedExtension = settings.value(QStringLiteral("goatedExtension"), false).toBool();
    m_live.wolfbetExtension = settings.value(QStringLiteral("wolfbetExtension"), false).toBool();
    m_live.duelExtension = settings.value(QStringLiteral("duelExtension"), false).toBool();
    m_live.paradiceExtension = settings.value(QStringLiteral("paradiceExtension"), false).toBool();
    m_live.pasinoExtension = settings.value(QStringLiteral("pasinoExtension"), false).toBool();
    m_live.towerExtension = settings.value(QStringLiteral("towerExtension"), false).toBool();
    m_live.winnaExtension = settings.value(QStringLiteral("winnaExtension"), false).toBool();
    m_currencyByCasino[0] = settings.value(QStringLiteral("currency")).toString();
    m_currencyByCasino[1] = settings.value(QStringLiteral("shuffleCurrency")).toString();
    m_currencyByCasino[2] = settings.value(QStringLiteral("goatedCurrency")).toString();
    m_currencyByCasino[3] = settings.value(QStringLiteral("wolfbetCurrency")).toString();
    m_currencyByCasino[4] = settings.value(QStringLiteral("duelCurrency")).toString();
    m_currencyByCasino[5] = settings.value(QStringLiteral("paradiceCurrency")).toString();
    m_currencyByCasino[6] = settings.value(QStringLiteral("pasinoCurrency")).toString();
    m_currencyByCasino[7] = settings.value(QStringLiteral("towerCurrency")).toString();
    m_currencyByCasino[8] = settings.value(QStringLiteral("winnaCurrency")).toString();
    m_live.currency = m_currencyByCasino[int(m_live.casino)];
	m_live.enabled = settings.value(QStringLiteral("enabled"), false).toBool();
	
    // API key, cookie, tokens and IDs are always saved on this computer.
    m_live.apiKey = settings.value(QStringLiteral("apiKey")).toString();
    m_live.clearance = settings.value(QStringLiteral("clearance")).toString();
    m_live.userAgent = settings.value(QStringLiteral("userAgent")).toString();
    m_live.shuffleToken = settings.value(QStringLiteral("shuffleToken")).toString();
    m_live.goatedToken = settings.value(QStringLiteral("goatedToken")).toString();
    m_live.wolfbetToken = settings.value(QStringLiteral("wolfbetToken")).toString();
    m_live.paradiceToken = settings.value(QStringLiteral("paradiceToken")).toString();
    m_live.pasinoToken = settings.value(QStringLiteral("pasinoToken")).toString();
    m_live.towerToken = settings.value(QStringLiteral("towerToken")).toString();
    m_live.towerClearance = settings.value(QStringLiteral("towerClearance")).toString();
    m_live.towerUserAgent = settings.value(QStringLiteral("towerUserAgent")).toString();
    m_live.winnaSession = settings.value(QStringLiteral("winnaSession")).toString();
    m_live.winnaClearance = settings.value(QStringLiteral("winnaClearance")).toString();
    m_live.winnaUserAgent = settings.value(QStringLiteral("winnaUserAgent")).toString();
    m_live.winnaDeviceId = settings.value(QStringLiteral("winnaDeviceId")).toString();
    m_live.duelSite = LiveSettings::hostOf(
        settings.value(QStringLiteral("duelSite"), m_live.duelSite).toString(), QStringLiteral("duel.com"));
    m_live.duelToken = settings.value(QStringLiteral("duelToken")).toString();
    m_live.duelUuid = settings.value(QStringLiteral("duelUuid")).toString();
    m_live.duelClearance = settings.value(QStringLiteral("duelClearance")).toString();
    m_live.duelUserAgent = settings.value(QStringLiteral("duelUserAgent")).toString();
    m_live.correlationUserId = settings.value(QStringLiteral("correlationUserId")).toString();
    m_live.browserId = settings.value(QStringLiteral("browserId")).toString();
    settings.endGroup();
    updateCasinoMenu();

    BotConfig cfg;
    settings.beginGroup(QStringLiteral("AutoBet"));
    cfg.baseBet = settings.value(QStringLiteral("baseBet"), cfg.baseBet).toDouble();
    cfg.stopAfterBets = settings.value(QStringLiteral("betCount"), cfg.stopAfterBets).toInt();
    cfg.onWin.increase = settings.value(QStringLiteral("onWinIncrease"), cfg.onWin.increase).toBool();
    cfg.onWin.pct = settings.value(QStringLiteral("onWinPct"), cfg.onWin.pct).toDouble();
    cfg.onLoss.increase = settings.value(QStringLiteral("onLossIncrease"), cfg.onLoss.increase).toBool();
    cfg.onLoss.pct = settings.value(QStringLiteral("onLossPct"), cfg.onLoss.pct).toDouble();
    cfg.stopProfit = settings.value(QStringLiteral("stopProfit"), cfg.stopProfit).toDouble();
    cfg.stopLoss = settings.value(QStringLiteral("stopLoss"), cfg.stopLoss).toDouble();
    settings.endGroup();
    m_autoPanel->setFrom(cfg);
    {
        // Saved together; when missing (older settings) the panel keeps its 2x / 49.5% default.
        const QVariant mult = settings.value(QStringLiteral("AutoBet/multiplier"));
        const QVariant chance = settings.value(QStringLiteral("AutoBet/winChance"));
        if (mult.isValid() && chance.isValid())
            m_autoPanel->setOdds(mult.toDouble(), chance.toDouble());
    }
    m_autoPanel->setConditionCount(m_conditions.size());
    m_autoPanel->setUseConditions(settings.value(QStringLiteral("AutoBet/useConditions"), false).toBool());

    // Shared theme (all windows). Older versions saved it per window: use the main
    // window's value until the shared one is written.
    bool dark = false;
    {
        QSettings global;
        if (global.contains(QLatin1String(kThemeKey)))
            dark = global.value(QLatin1String(kThemeKey)).toBool();
        else
            dark = global.value(WindowSettings::groupFor(0) + QStringLiteral("/General/darkTheme"),
                                settings.value(QStringLiteral("General/darkTheme"), false))
                       .toBool();
    }
    {
        const QSignalBlocker blocker(m_actDarkTheme); // loading must not restyle other windows
        m_actDarkTheme->setChecked(dark);
    }
    applyTheme(dark); // setChecked only fires toggled when the value changes
}

// Everything the Options dialog edits: general options and live betting (casino, sites,
// currencies, API key, cookie, tokens and IDs). Written on exit and when Options is OK'd.
void MainWindow::saveOptionsSettings(QSettings &settings)
{
    settings.setValue(QStringLiteral("General/currency"), m_currencyTag);
    settings.setValue(QStringLiteral("General/startBalance"), m_startBalance);
    settings.setValue(QStringLiteral("General/clientSeed"), m_clientSeed);
    settings.setValue(QStringLiteral("General/randomClientSeed"), m_randomClientSeed);
    settings.setValue(QStringLiteral("General/betDelayMs"), m_betDelayMs);
    settings.setValue(QStringLiteral("General/retryNoBalance"), m_retryNoBalance);
    settings.setValue(QStringLiteral("General/game"), gameName(m_game));
    settings.setValue(QStringLiteral("Manual/betAmount"), m_manualPanel->amount());
    saveManualTargets(settings);
    settings.remove(QStringLiteral("General/darkTheme")); // the theme is shared, see kThemeKey
    QSettings().setValue(QLatin1String(kThemeKey), m_actDarkTheme->isChecked());

    m_currencyByCasino[int(m_live.casino)] = m_live.currency;
    settings.beginGroup(QStringLiteral("Live"));
    settings.setValue(QStringLiteral("casino"), m_live.casinoName());
    settings.setValue(QStringLiteral("site"), m_live.site);
    settings.setValue(QStringLiteral("shuffleSite"), m_live.shuffleSite);
    settings.setValue(QStringLiteral("extensionMode"), m_live.extensionMode);
    settings.setValue(QStringLiteral("shuffleExtension"), m_live.shuffleExtension);
    settings.setValue(QStringLiteral("goatedExtension"), m_live.goatedExtension);
    settings.setValue(QStringLiteral("wolfbetExtension"), m_live.wolfbetExtension);
    settings.setValue(QStringLiteral("duelExtension"), m_live.duelExtension);
    settings.setValue(QStringLiteral("paradiceExtension"), m_live.paradiceExtension);
    settings.setValue(QStringLiteral("pasinoExtension"), m_live.pasinoExtension);
    settings.setValue(QStringLiteral("towerExtension"), m_live.towerExtension);
    settings.setValue(QStringLiteral("winnaExtension"), m_live.winnaExtension);
    settings.setValue(QStringLiteral("currency"), m_currencyByCasino[0]);
    settings.setValue(QStringLiteral("shuffleCurrency"), m_currencyByCasino[1]);
    settings.setValue(QStringLiteral("goatedCurrency"), m_currencyByCasino[2]);
    settings.setValue(QStringLiteral("wolfbetCurrency"), m_currencyByCasino[3]);
    settings.setValue(QStringLiteral("duelCurrency"), m_currencyByCasino[4]);
    settings.setValue(QStringLiteral("paradiceCurrency"), m_currencyByCasino[5]);
    settings.setValue(QStringLiteral("pasinoCurrency"), m_currencyByCasino[6]);
    settings.setValue(QStringLiteral("towerCurrency"), m_currencyByCasino[7]);
    settings.setValue(QStringLiteral("winnaCurrency"), m_currencyByCasino[8]);
    settings.remove(QStringLiteral("remember")); // old "Remember…" checkbox, no longer used
	settings.setValue(QStringLiteral("enabled"), m_live.enabled);
	
    settings.setValue(QStringLiteral("apiKey"), m_live.apiKey);
    settings.setValue(QStringLiteral("clearance"), m_live.clearance);
    settings.setValue(QStringLiteral("userAgent"), m_live.userAgent);
    settings.setValue(QStringLiteral("shuffleToken"), m_live.shuffleToken);
    settings.setValue(QStringLiteral("goatedToken"), m_live.goatedToken);
    settings.setValue(QStringLiteral("wolfbetToken"), m_live.wolfbetToken);
    settings.setValue(QStringLiteral("paradiceToken"), m_live.paradiceToken);
    settings.setValue(QStringLiteral("pasinoToken"), m_live.pasinoToken);
    settings.setValue(QStringLiteral("towerToken"), m_live.towerToken);
    settings.setValue(QStringLiteral("towerClearance"), m_live.towerClearance);
    settings.setValue(QStringLiteral("towerUserAgent"), m_live.towerUserAgent);
    settings.setValue(QStringLiteral("winnaSession"), m_live.winnaSession);
    settings.setValue(QStringLiteral("winnaClearance"), m_live.winnaClearance);
    settings.setValue(QStringLiteral("winnaUserAgent"), m_live.winnaUserAgent);
    settings.setValue(QStringLiteral("winnaDeviceId"), m_live.winnaDeviceId);
    settings.setValue(QStringLiteral("duelSite"), m_live.duelSite);
    settings.setValue(QStringLiteral("duelToken"), m_live.duelToken);
    settings.setValue(QStringLiteral("duelUuid"), m_live.duelUuid);
    settings.setValue(QStringLiteral("duelClearance"), m_live.duelClearance);
    settings.setValue(QStringLiteral("duelUserAgent"), m_live.duelUserAgent);
    settings.setValue(QStringLiteral("correlationUserId"), m_live.correlationUserId);
    settings.setValue(QStringLiteral("browserId"), m_live.browserId);
    settings.endGroup();
}

void MainWindow::saveManualTargets(QSettings &settings)
{
    settings.setValue(QStringLiteral("Manual/diceTarget"), m_manualPanel->target());
    settings.setValue(QStringLiteral("Manual/diceUnder"), m_manualPanel->side() == BetSide::Under);
    settings.setValue(QStringLiteral("Manual/limboTarget"), m_manualPanel->limboTarget());
}

void MainWindow::saveSettings()
{
    saveConditions();
    ScopedSettings settings(settingsGroup());
	
    settings.setValue(QStringLiteral("MainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("MainWindow/state"), saveState());
    settings.setValue(QStringLiteral("MainWindow/sidebarDockSaved"), true); // saveState() knows the sidebar dock
    settings.setValue(QStringLiteral("MainWindow/betListDockSaved"), true); // saveState() knows the bet list dock
    settings.setValue(QStringLiteral("MainWindow/bottomTab"), m_tabs->currentIndex());
    settings.setValue(QStringLiteral("MainWindow/logHeight"), m_logHeight);
    settings.setValue(QStringLiteral("MainWindow/tableState2"),
                      m_betTable->horizontalHeader()->saveState());
    m_luaDock->saveSettings();
    settings.setValue(QStringLiteral("View/sidebar"), m_actShowSidebar->isChecked());
    settings.setValue(QStringLiteral("View/betList"), m_actShowBetList->isChecked());
    settings.setValue(QStringLiteral("View/bottomPanel"), m_actShowTabs->isChecked());

    saveOptionsSettings(settings);

    BotConfig cfg;
    m_autoPanel->applyTo(cfg);
    settings.beginGroup(QStringLiteral("AutoBet"));
    settings.setValue(QStringLiteral("baseBet"), cfg.baseBet);
    settings.setValue(QStringLiteral("betCount"), cfg.stopAfterBets);
    settings.setValue(QStringLiteral("onWinIncrease"), cfg.onWin.increase);
    settings.setValue(QStringLiteral("onWinPct"), cfg.onWin.pct);
    settings.setValue(QStringLiteral("onLossIncrease"), cfg.onLoss.increase);
    settings.setValue(QStringLiteral("onLossPct"), cfg.onLoss.pct);
    settings.setValue(QStringLiteral("stopProfit"), cfg.stopProfit);
    settings.setValue(QStringLiteral("stopLoss"), cfg.stopLoss);
    settings.setValue(QStringLiteral("multiplier"), m_autoPanel->multiplier());
    settings.setValue(QStringLiteral("winChance"), m_autoPanel->winChance());
    settings.remove(QStringLiteral("delayMs")); // old auto-bet panel delay, no longer used
    settings.setValue(QStringLiteral("useConditions"), m_autoPanel->useConditions());
    settings.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_engine->isRunning())
        m_engine->stop(tr("application closing"));

    // Flush the Fair panel's inputs in case the user typed a seed and closed
    // the window without unfocusing the field (editingFinished never fires in
    // that case).
    if (m_fairPanel && !m_discardSettings)
        m_fairPanel->saveSettings();

    // --- ADD THIS BLOCK ---
    if (m_hiddenBrowser) {
        m_hiddenBrowser->close();
        delete m_hiddenBrowser; // This triggers the destructor (and the Close() call above)
        m_hiddenBrowser = nullptr;
    }
    if (m_webTransport) {
        delete m_webTransport;
        m_webTransport = nullptr;
    }
    if (m_duelBrowser) {
        m_duel->setBrowser(nullptr);
        m_duelBrowser->close();
        delete m_duelBrowser;
        m_duelBrowser = nullptr;
    }
    if (m_shuffle)
        m_shuffle->closeSocket();
    // ----------------------

    // Every window saves into its own group ("Windows/<slot>"), so the next launch (main
    // window) and the next "+" (window 2, 3, …) get back exactly this window's data.
    if (!m_discardSettings)
        saveSettings();
    event->accept();
}

// ---------------------------------------------------------------- per-window data

QString MainWindow::settingsGroup() const
{
    return WindowSettings::groupFor(m_slot);
}

QList<int> &MainWindow::openSlots()
{
    static QList<int> open;
    return open;
}

int MainWindow::freeSlot()
{
    int slot = 0;
    while (openSlots().contains(slot))
        ++slot;
    return slot;
}

void MainWindow::copySettingsGroup(const QString &from, const QString &to,
                                   const QStringList &skip)
{
    QSettings settings;
    settings.remove(to);
    settings.beginGroup(from);
    QStringList keys;
    QList<QVariant> values;
    for (const QString &key : settings.allKeys()) {
        bool skipped = false;
        for (const QString &prefix : skip)
            skipped = skipped || key.startsWith(prefix);
        if (skipped)
            continue;
        keys << key;
        values << settings.value(key);
    }
    settings.endGroup();
    for (int i = 0; i < keys.size(); ++i)
        settings.setValue(to + QLatin1Char('/') + keys.at(i), values.at(i));
}

void MainWindow::migrateLegacySettings()
{
    QSettings settings;
    if (settings.childGroups().contains(QStringLiteral("Windows")))
        return; // already per-window
    // Older versions saved one set of data at the root, shared by every window: it becomes
    // the main window's. The old keys are left in place.
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys) {
        if (!key.startsWith(QLatin1String("Session/")))
            settings.setValue(WindowSettings::groupFor(0) + QLatin1Char('/') + key,
                              settings.value(key));
    }
    settings.sync();
}
