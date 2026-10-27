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
#include "bot/GoatedTransport.h"
#include "bot/WolfbetTransport.h"
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
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
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

namespace
{

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
class FixedWidthHolder : public QWidget
{
public:
    explicit FixedWidthHolder(QWidget *child, QWidget *parent = nullptr)
        : QWidget(parent), m_child(child)
    {
        m_child->setParent(this);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    }

    QSize sizeHint() const override { return m_child->sizeHint(); }
    QSize minimumSizeHint() const override { return QSize(0, 0); }

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

private:
    void relayout()
    {
        int w = width();
        // Width available if no dock took space on the right: from this widget's
        // left edge to the right edge of the main window.
        if (QWidget *top = window(); top && top != this)
            w = std::max(w, top->width() - mapTo(top, QPoint(0, 0)).x());
        // Before the child is resized, so a page limited to the visible width is
        // already capped (or released again) when the child's layout runs.
        if (m_visibleWidthCb)
            m_visibleWidthCb(width());
        m_child->setGeometry(0, 0, w, height());
    }

    QWidget *m_child;
    std::function<void(int)> m_visibleWidthCb;
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
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
    connect(m_ext, &ExtensionTransport::originRejected, this, [this](const QString &origin) {
        onLogMessage(tr("Rejected a connection to the extension port from origin \"%1\". Allowed: "
                        "browser extensions and https://%2 — set Site/mirror in Options if your "
                        "extension runs on another domain.")
                         .arg(origin, m_live.site),
                     true);
    });

    createActions();
    createMenus();
    createSidebar();
    createCentralWidget();
    createToolbar();
    createStatusBar();
    createLogDock();
    createLuaDock();

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
    connect(m_autoPanel, &AutoBetPanel::startRequested, this, &MainWindow::onStartAuto);
    connect(m_autoPanel, &AutoBetPanel::stopRequested, this, &MainWindow::onStopAuto);
    connect(m_fairPanel, &FairPanel::seedRotated, this,
            [this](const QString &msg) { onLogMessage(msg, false); });

    // Default size for a first launch; loadSettings() replaces it with the
    // geometry from the previous session when one was saved.
    resize(1040, 560);

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
        if (m_luaDock)
            m_luaDock->setVisible(visible);
    });

    m_actDarkTheme = new QAction(tr("Dark theme"), this);
    m_actDarkTheme->setCheckable(true);
    connect(m_actDarkTheme, &QAction::toggled, this, &MainWindow::onToggleDarkTheme);

    m_actShowLog = new QAction(tr("Show log"), this);
    m_actShowLog->setCheckable(true);
    connect(m_actShowLog, &QAction::toggled, this, [this](bool visible) {
        if (m_logDock)
            m_logDock->setVisible(visible);
    });

    // Panel toggles (View menu). The panels themselves are created later, so
    // the actions look the widget up through the member pointer when toggled.
    m_actShowSidebar = new QAction(tr("Show sidebar"), this);
    m_actShowSidebar->setCheckable(true);
    m_actShowSidebar->setChecked(true);
    connect(m_actShowSidebar, &QAction::toggled, this, [this](bool visible) {
        if (m_sidebar)
            m_sidebar->setVisible(visible);
    });

    m_actShowBetList = new QAction(tr("Show bet list"), this);
    m_actShowBetList->setCheckable(true);
    m_actShowBetList->setChecked(true);
    connect(m_actShowBetList, &QAction::toggled, this, [this](bool visible) {
        if (m_betTable)
            m_betTable->setVisible(visible);
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
    });
}

void MainWindow::createMenus()
{
    // Casino: which site the live bets go to. Stake keeps its login methods; Shuffle uses
    // "Direct mode" (plain HTTPS + websocket, no WebView2 / browser extension).
    QMenu *casinoMenu = menuBar()->addMenu(tr("&Casino"));
    m_casinoGroup = new QActionGroup(this);
    m_casinoGroup->setExclusive(true);
    m_actCasinoStake = casinoMenu->addAction(tr("&Stake"));
    m_actCasinoShuffle = casinoMenu->addAction(tr("S&huffle"));
    m_actCasinoGoated = casinoMenu->addAction(tr("&Goated"));
    m_actCasinoWolfbet = casinoMenu->addAction(tr("&Wolfbet"));
    for (QAction *a : {m_actCasinoStake, m_actCasinoShuffle, m_actCasinoGoated, m_actCasinoWolfbet}) {
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
    scroll->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
    m_sidebar = scroll;
    m_sidebar->setFixedWidth(210);
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
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(8.5);
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

    m_fairPanel = new FairPanel(m_provider);

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

    m_mainSplitter = new QSplitter(Qt::Vertical);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    m_mainSplitter->addWidget(m_betTable);
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
    m_mainSplitter->addWidget(tabsHolder);
    m_mainSplitter->setStretchFactor(0, 1); // 1 = Table takes all extra window space
    m_mainSplitter->setStretchFactor(1, 0); // 0 = Tabs stay at the exact size you dragged
    m_mainSplitter->setSizes({600, 200});

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_mainSplitter);

    m_centralSplitter = new QSplitter(Qt::Horizontal);
    m_centralSplitter->setObjectName(QStringLiteral("centralSplitter"));
    m_centralSplitter->setChildrenCollapsible(false);
    m_centralSplitter->addWidget(m_sidebar);
    m_centralSplitter->addWidget(right);
    m_centralSplitter->setStretchFactor(0, 0);
    m_centralSplitter->setStretchFactor(1, 1);
    m_centralSplitter->setSizes({192, 1000});
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
}

void MainWindow::createLuaDock()
{
    // The right dock area owns the corners: the script editor runs the full window
    // height, above the bottom (Log) dock instead of being cut short by it.
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    m_luaDock = new LuaEditorDock(this);
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
    setWindowTitle(tr("xBot - %1").arg(casinoDisplayName(m_live.casino)));
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
    if (m_engine->isRunning()) {
        // The running bot (or its script) decides the game; undo the click.
        (m_game == Game::Limbo ? m_actGameLimbo : m_actGameDice)->setChecked(true);
        onLogMessage(tr("Stop the running auto-bet before changing the game."), true);
        return;
    }
    setGame(game);
}

QString MainWindow::casinoDisplayName(LiveSettings::Casino casino)
{
    switch (casino) {
    case LiveSettings::Casino::Shuffle: return tr("Shuffle");
    case LiveSettings::Casino::Goated:  return tr("Goated");
    case LiveSettings::Casino::Wolfbet: return tr("Wolfbet");
    default:                            return tr("Stake");
    }
}

QAction *MainWindow::casinoAction(LiveSettings::Casino casino) const
{
    switch (casino) {
    case LiveSettings::Casino::Shuffle: return m_actCasinoShuffle;
    case LiveSettings::Casino::Goated:  return m_actCasinoGoated;
    case LiveSettings::Casino::Wolfbet: return m_actCasinoWolfbet;
    default:                            return m_actCasinoStake;
    }
}

void MainWindow::updateCasinoMenu()
{
    if (!m_actCasinoStake || !m_actCasinoShuffle || !m_actCasinoGoated || !m_actCasinoWolfbet)
        return;
    const QSignalBlocker blocker(m_casinoGroup);
    casinoAction(m_live.casino)->setChecked(true);
}

void MainWindow::onCasinoSelected(LiveSettings::Casino casino)
{
    if (m_live.casino == casino)
        return;
    if (m_engine->isRunning() || m_engine->betInFlight()) {
        updateCasinoMenu(); // undo the click: the running bot keeps its casino
        onLogMessage(tr("Stop the running auto-bet before changing the casino."), true);
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
        BotConfig cfg = currentConfig();
        cfg.baseBet = m_manualPanel->amount();
        m_engine->rollOnce(cfg);
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
        const BotConfig cfg = currentConfig();

        m_engine->setConfig(cfg);
        m_engine->setConditions(m_conditions);
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
    if (!fromScript)
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
        || clickedCasino.compare(QLatin1String("wolfbet"), Qt::CaseInsensitive) == 0)
        return;                      // Shuffle / Goated / Wolfbet ids are used as they are
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

    // Goated: the bet id can only be copied (no public bet page is known).
    if (goatedRow) {
        QMenu menu;
        QAction *copyAct = menu.addAction(tr("Copy Bet id:  %1").arg(betId));
        if (menu.exec(globalPos) == copyAct)
            QApplication::clipboard()->setText(betId);
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
    m_tabs->setCurrentIndex(2);
}

// When switching to Provably Fair / Auto Bet, resize the bottom pane so the
// page's content fits exactly. FairPanel gets a safety floor (its hint is 0
// on the very first switch, before it has laid itself out); AutoBetPanel uses
// QSizePolicy::Maximum, so its sizeHint() is already the tight natural height.
void MainWindow::onTabChanged(int index)
{
    QWidget *page = m_tabs->widget(index);
    if (!page)
        return;

    // Tab pages keep their natural height (see the event filter below), so the
    // Log dock can only clip them, never squeeze them. Here we just give the
    // panel its full content height when there is room for it.
    auto fit = [this, page]() {
        if (m_tabs->currentWidget() != page)
            return;
        if (QLayout *l = page->layout())
            l->activate();

        const int tabBarH = m_tabs->tabBar()->sizeHint().height();
        const int contentH = std::max({page->sizeHint().height(),
                                       page->minimumSizeHint().height(),
                                       page->minimumHeight(),
                                       80});
        const int needed = contentH + tabBarH + 8;   // small buffer for the pane border

        const QList<int> sizes = m_mainSplitter->sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (total <= 0)
            return;

        const int minTableH = 60;
        const int tabsH = qMin(needed, qMax(0, total - minTableH));
        m_mainSplitter->setSizes({total - tabsH, tabsH});
    };

    fit();
    QTimer::singleShot(0, this, fit);
}

void MainWindow::onToggleDarkTheme(bool enabled)
{
    applyTheme(enabled);
}

void MainWindow::onOpenFile()
{
    // "+" button: open a new, independent instance of the main window.
    // It is deleted automatically when closed; the app quits when the last
    // window is closed.
    // The new window loads the latest saved data on its own (loadSettings()
    // runs in the MainWindow constructor).
    auto *window = new MainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);

    // Start with the same theme as the window it was opened from; after that the
    // two windows switch themes independently.
    window->m_actDarkTheme->setChecked(m_actDarkTheme->isChecked());

    // Cascade the new window so it doesn't sit exactly on top of this one.
    window->move(pos() + QPoint(30, 30));
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::onOpenConditionBuilder()
{
    ConditionBuilderDialog dialog(m_conditions, m_strategies, m_activeStrategy, this);
    dialog.setWindowTitle(tr("Condition Builder — %1").arg(gameName(m_game).toUpper()));
    if (dialog.exec() != QDialog::Accepted) {
        // The dialog autosaves strategies.json as you edit, even if you cancel;
        // re-read it so the in-memory copy matches the file.
        if (QFile::exists(ConditionIo::strategiesPath()))
            m_strategies = ConditionIo::loadStrategies();
        return;
    }

    m_conditions = dialog.result();
    m_strategies = dialog.resultStrategies();
    m_activeStrategy = dialog.resultStrategyName();
    saveConditions();
    ConditionIo::saveStrategies(m_strategies);

    QString msg = tr("Conditions saved (%1 blocks)").arg(m_conditions.size());
    if (!m_activeStrategy.isEmpty())
        msg += tr(" — strategy: \"%1\"").arg(m_activeStrategy);
    onLogMessage(msg, false);
}

void MainWindow::loadConditions()
{
    QSettings settings;
    const QByteArray raw = settings.value(QStringLiteral("Conditions/blocks")).toByteArray();
    m_conditions = ConditionIo::blocksFromJson(QJsonDocument::fromJson(raw).array());
    m_activeStrategy = settings.value(QStringLiteral("Conditions/activeStrategy")).toString();
    m_strategies = ConditionIo::loadStrategies();
}

void MainWindow::saveConditions()
{
    QSettings settings;
    settings.setValue(QStringLiteral("Conditions/blocks"),
                      QJsonDocument(ConditionIo::blocksToJson(m_conditions)).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("Conditions/activeStrategy"), m_activeStrategy);
}

void MainWindow::onOpenOptions()
{
    OptionsDialog dialog(this);
    m_optionsDialog = &dialog;
    dialog.setDarkTheme(m_actDarkTheme->isChecked());
    dialog.setCurrencyTag(m_currencyTag);
    dialog.setStartBalance(m_startBalance);
    dialog.setClientSeed(m_clientSeed);
    dialog.setRandomClientSeed(m_randomClientSeed);
    dialog.setLiveSettings(m_live);
    if (!m_live.currency.isEmpty())
        dialog.setLiveCurrencies({m_live.currency}, m_live.currency);
    dialog.setExtensionStatus(m_ext->isListening(), m_ext->isConnected());

    connect(m_ext, &IStakeTransport::readyChanged, &dialog, [this, &dialog](bool) {
        dialog.setExtensionStatus(m_ext->isListening(), m_ext->isConnected());
    });
    connect(&dialog, &OptionsDialog::loginRequested, this, [this, &dialog] {
        QString err;
        if (!configureConnection(dialog.liveSettings(), &err)) {
            dialog.setLoginStatus(err, false);
            return;
        }
        dialog.setExtensionStatus(m_ext->isListening(), m_ext->isConnected());

        // Shuffle direct mode talks to Shuffle straight from the app (HTTPS + websocket) and
        // never uses the hidden WebView2 browser (which only loads Stake's site), so there is
        // nothing to wait for. Waiting on it made the login hang on "Loading browser…"
        // whenever that unrelated Stake page was slow or failed to load.
        const LiveSettings cur = dialog.liveSettings();
        const bool needsBrowser = !cur.isShuffle() && !cur.isGoated() && !cur.isWolfbet() && !cur.extensionMode;
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
    m_actDarkTheme->setChecked(dialog.darkTheme());

    if (m_engine->isRunning() || m_engine->betInFlight()) {
        configureConnection(m_live);
        onLogMessage(tr("Live betting settings are not changed while a bet or auto-bet is running."),
                     true);
    } else {
        applyLiveSettings(dialog.liveSettings());
    }

    updateSessionWidgets();
    updateStatusWidgets();
    onLogMessage(tr("Options applied (currency: %1)").arg(currencyLabel()), false);
}

// ---------------------------------------------------------------- live betting

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
        m_hiddenBrowser->setAttribute(Qt::WA_DontShowOnScreen);
        m_hiddenBrowser->show();
        m_browserSite = s.site;
        m_browserUA = s.userAgent;

        // Fetch the balance as soon as the page is actually loaded.
        connect(m_hiddenBrowser, &CookieGrabberDialog::pageLoaded, this, [this] {
            if (m_live.enabled && !m_live.isShuffle() && !m_live.isGoated() && !m_live.isWolfbet() && m_stake)
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

// Stake's session (extension server or hidden WebView2 browser) is kept up while Goated is the
// active casino, like it already is for Shuffle, so Stake's balance can be read in the
// background and a switch back finds it connected. Only when Stake credentials are set.
void MainWindow::keepStakeSessionAlive(const LiveSettings &s)
{
    if (s.apiKey.isEmpty() && !s.extensionMode)
        return;
    if (s.extensionMode) {
        StakeCredentials cred;
        cred.site = s.site;
        cred.apiKey = s.apiKey;
        cred.clearance = s.clearance;
        cred.userAgent = s.userAgent;
        m_ext->setCredentials(cred);
        QString extErr;
        if (!m_ext->start(s.extensionPort, &extErr))
            onLogMessage(tr("Could not start the extension server on 127.0.0.1:%1 (%2)")
                             .arg(s.extensionPort)
                             .arg(extErr),
                         true);
    } else {
        m_ext->stop();
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
    requestStakeBackgroundBalance();
}

void MainWindow::onStakeBackgroundBalances(const QMap<QString, double> &balances)
{
    if (!m_live.enabled || m_live.casino == LiveSettings::Casino::Stake)
        return; // Stake is active: its normal fetch fills the cache
    m_stakeBalances = balances;
    refreshStakeBalanceVar();
}

bool MainWindow::configureConnection(const LiveSettings &s, QString *errorOut)
{
    if (s.isWolfbet()) {
        // Direct mode: plain HTTPS straight from the app, only the token is needed. The
        // Stake browser / extension are left as they are for a quick switch back.
        applyWolfbetCredentials(s);
        applyGoatedCredentials(s); // so Goated's balance can be read in the background

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
        if (s.extensionMode) {
            // Keep the extension server (and its connected browser) alive while Shuffle is
            // active, so switching back to Stake finds it still connected.
            StakeCredentials cred;
            cred.site = s.site;
            cred.apiKey = s.apiKey;
            cred.clearance = s.clearance;
            cred.userAgent = s.userAgent;
            m_ext->setCredentials(cred);
            QString extErr;
            if (!m_ext->start(s.extensionPort, &extErr))
                onLogMessage(tr("Could not start the extension server on 127.0.0.1:%1 (%2)")
                                 .arg(s.extensionPort)
                                 .arg(extErr),
                             true);
        } else {
            m_ext->stop();
            ensureHiddenBrowser(s); // keep WebView2 open for a quick switch back to Stake
            m_webTransport->setBrowser(m_hiddenBrowser); // so it can read Stake's balance
        }
        applyGoatedCredentials(s); // so Goated's balance can be read in the background
        applyWolfbetCredentials(s); // so Wolfbet's balance can be read in the background
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

    StakeCredentials cred;
    cred.site = s.site;
    cred.apiKey = s.apiKey;
    cred.clearance = s.clearance;
    cred.userAgent = s.userAgent;
    m_cookie->setCredentials(cred);
    m_ext->setCredentials(cred);

    if (s.extensionMode) {
        QString err;
        if (!m_ext->start(s.extensionPort, &err)) {
            const QString msg = tr("Could not start the extension server on 127.0.0.1:%1 (%2)")
                                    .arg(s.extensionPort)
                                    .arg(err);
            if (errorOut)
                *errorOut = msg;
            onLogMessage(msg, true);
            return false;
        }
        m_stake->setTransport(m_ext);
    } else {
        m_ext->stop();
        
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
        fetchActiveBalance(4000);       // not again within 4 s: quick script switches would hit rate limits
        // The casino just left is read again right away (exact server figure), the others too.
        refreshBackgroundBalances();
        m_balanceTimer->setInterval(m_isLoggedIn ? 10000 : 2000); // quick retries until logged in
        m_balanceTimer->start();
        onLogMessage(tr("Live betting enabled (%1, %2). Fetching balance…")
                         .arg(m_live.activeSite(),
                              (m_live.isShuffle() || m_live.isGoated() || m_live.isWolfbet()) ? tr("direct")
                                                 : (m_live.extensionMode ? tr("extension") : tr("cookie"))),
                     false);
    } else {
        m_balanceTimer->stop();
        updateShuffleBackground(m_live); // live is off: stops and closes the Shuffle socket
        if (modeChanged)
            onLogMessage(tr("Back to simulation (play money)."), false);
    }
    updateModeWidgets();
}

void MainWindow::onBalancesReady(const QMap<QString, double> &balances)
{
    m_balances = balances;
    // Only Stake's own answers (a login test for another casino must not land in its cache).
    if (m_live.enabled && m_live.casino == LiveSettings::Casino::Stake
        && !m_stake->isShuffle() && !m_stake->isGoated() && !m_stake->isWolfbet()) {
        m_stakeBalances = balances;
        refreshStakeBalanceVar();
    }
    m_lastBalanceError.clear();
    const QStringList codes = balances.keys();
	m_isLoggedIn = true; 
    // Pick the currency: what the dialog (if open) or the saved settings say,
    // otherwise the first one the account has.
    // Shuffle pushes one currency at a time, so a chosen currency whose balance has not
    // arrived yet is waited for instead of being replaced by whichever arrived first.
    const bool shuffle = m_live.isShuffle();
    QString cur = m_optionsDialog ? m_optionsDialog->liveSettings().currency : m_live.currency;
    if (cur.isEmpty() || (!shuffle && !balances.contains(cur))) {
        cur = codes.value(0);
        if (m_live.isWolfbet()) {
            // Wolfbet lists every currency (most at 0, plus the "free" bonus balance): start with
            // the first one that actually holds funds.
            for (auto it = balances.constBegin(); it != balances.constEnd(); ++it) {
                if (it.value() > 0.0 && it.key() != QLatin1String("free")) {
                    cur = it.key();
                    break;
                }
            }
        }
    }

    if (m_optionsDialog) {
        QStringList shown = codes;
        if (!cur.isEmpty() && !shown.contains(cur))
            shown.prepend(cur);
        m_optionsDialog->setLiveCurrencies(shown, cur);
        if (balances.contains(cur))
            m_optionsDialog->setLoginStatus(
                tr("Logged in — %1 %2 available")
                    .arg(formatAmount(balances.value(cur), 8), cur.toUpper()),
                true);
        else
            m_optionsDialog->setLoginStatus(
                tr("Connected — waiting for the %1 balance…").arg(cur.toUpper()), true);
    }

    if (!m_live.enabled)
        return;

    if (m_live.currency.isEmpty() || (!shuffle && !balances.contains(m_live.currency))) {
        m_live.currency = cur;
        m_stake->setCurrency(cur);
        refreshShuffleBalanceVar();
        refreshGoatedBalanceVar();
        refreshWolfbetBalanceVar();
        refreshStakeBalanceVar();
        onLogMessage(tr("Betting currency set to %1.").arg(cur.toUpper()), false);
    }
    if (!balances.contains(m_live.currency)) {
        updateConnectionStatus(); // Shuffle: the balance of the chosen currency is still to come
        return;
    }

    const double balance = balances.value(m_live.currency);
    // Shuffle's websocket reports the new balance right after each bet, usually before the
    // bet's own answer is processed. Re-syncing then would count that bet twice, so while a
    // bet is pending or an auto-bet runs the engine's own running balance is kept.
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
    m_shuffleBalanceKnown = !cur.isEmpty() && m_shuffleBalances.contains(cur);
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
    if (cur.isEmpty() || !m_shuffleBalances.contains(cur) || !m_shuffle->hasFreshBalance(cur))
        return false;

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
    m_goatedBalanceKnown = !cur.isEmpty() && m_goatedBalances.contains(cur);
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
    m_wolfbetBalanceKnown = !cur.isEmpty() && m_wolfbetBalances.contains(cur);
    m_wolfbetBalance = m_wolfbetBalanceKnown ? m_wolfbetBalances.value(cur) : 0.0;
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
}

// Recompute m_stakeBalance (the Stake bet currency's balance) from the cache.
void MainWindow::refreshStakeBalanceVar()
{
    const QString cur = stakeCurrency();
    m_stakeBalanceKnown = !cur.isEmpty() && m_stakeBalances.contains(cur);
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
                                                              : m_stakeBalances;
    const QString cur = m_live.currency.trimmed().toLower();
    if (cur.isEmpty() || !cache.contains(cur))
        return false;

    m_balances = cache;
    m_engine->syncBalance(cache.value(cur));
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

void MainWindow::onExtensionReadyChanged(bool ready)
{
    if (ready)
        requestStakeBackgroundBalance(); // Shuffle / Goated active: fill Stake's cache
    if (!m_stake || m_stake->transport() != m_ext)
        return;
    if (ready) {
        onLogMessage(tr("Browser extension connected."), false);
        if (m_live.enabled || m_optionsDialog)
            m_stake->fetchBalances();
    } else {
        onLogMessage(tr("Browser extension disconnected."), true);
        m_isLoggedIn = false; // <--- ADD THIS LINE
    }
    updateModeWidgets();
}

void MainWindow::onBalanceTimer()
{
    if (!m_live.enabled)
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
                                       (m_live.isShuffle() || m_live.isGoated() || m_live.isWolfbet())
                                           ? tr("direct")
                                           : (m_live.extensionMode ? tr("extension")
                                                                   : tr("cookie")),
                                       (ready || m_live.isShuffle() || m_live.isGoated() || m_live.isWolfbet()
                                        || !m_live.extensionMode) ? QString() : tr(" (not connected)")));
        m_statusMode->setStyleSheet(QStringLiteral("color:#c9372a;font-weight:bold;"));
    } else {
        m_statusMode->setText(tr("Simulation"));
        m_statusMode->setStyleSheet(QStringLiteral("color:#737373;"));
    }

    // The Fairness tab verifies the *simulated* seeds only.
    const int fairIdx = m_tabs->indexOf(m_fairPanel);
    if (fairIdx >= 0)
        m_tabs->setTabEnabled(fairIdx, true);
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

    // The sidebar has a fixed width: a value may overlap its label, but its font is
    // shrunk if it would still be wider than the whole row.
    auto fitValue = [this](QLabel *lbl) {
        QFont f = lbl->font();
        f.setPointSizeF(8.5);
        const int rowW = m_sidebar->maximumWidth() - 16 /*grid margins*/ - 2 /*slack*/;
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
    // Keep every tab page at its natural height. Without a pinned minimum the
    // QTabWidget squeezes the page when the Log dock takes space, and the
    // controls overlap. With it, the page keeps its size and is clipped.
    if (m_tabs && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show)) {
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && m_tabs->indexOf(w) >= 0) {
            if (QLayout *l = w->layout()) {
                const int h = qMax(l->minimumSize().height(), l->sizeHint().height());
                if (w->minimumHeight() != h)
                    w->setMinimumHeight(h);
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
    QSettings settings;

    // No saved window state = first launch: Log and Lua docks start open.
    const bool firstLaunch = !settings.contains(QStringLiteral("MainWindow/state"));

    restoreGeometry(settings.value(QStringLiteral("MainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("MainWindow/state")).toByteArray());
    m_centralSplitter->restoreState(
        settings.value(QStringLiteral("MainWindow/centralSplitter")).toByteArray());
    m_mainSplitter->restoreState(
        settings.value(QStringLiteral("MainWindow/mainSplitter")).toByteArray());
    m_betTable->horizontalHeader()->restoreState(
        settings.value(QStringLiteral("MainWindow/tableState2")).toByteArray());

    // Panel visibility (View menu). Applied after the splitter state is
    // restored; setChecked() fires toggled() and shows/hides the panel.
    m_actShowSidebar->setChecked(settings.value(QStringLiteral("View/sidebar"), true).toBool());
    m_actShowBetList->setChecked(settings.value(QStringLiteral("View/betList"), true).toBool());
    m_actShowTabs->setChecked(settings.value(QStringLiteral("View/bottomPanel"), true).toBool());
    if (firstLaunch && m_logDock)
        m_logDock->show();
    {
        // The log dock's visibility comes from restoreState(); sync the menu.
        const QSignalBlocker blocker(m_actShowLog);
        m_actShowLog->setChecked(m_logDock && !m_logDock->isHidden());
    }
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
    m_clientSeed = settings.value(QStringLiteral("General/clientSeed")).toString();
    m_randomClientSeed = settings.value(QStringLiteral("General/randomClientSeed"), false).toBool();
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
    m_currencyByCasino[0] = settings.value(QStringLiteral("currency")).toString();
    m_currencyByCasino[1] = settings.value(QStringLiteral("shuffleCurrency")).toString();
    m_currencyByCasino[2] = settings.value(QStringLiteral("goatedCurrency")).toString();
    m_currencyByCasino[3] = settings.value(QStringLiteral("wolfbetCurrency")).toString();
    m_live.currency = m_currencyByCasino[int(m_live.casino)];
    m_live.remember = settings.value(QStringLiteral("remember"), false).toBool();
	m_live.enabled = settings.value(QStringLiteral("enabled"), false).toBool();
	
    if (m_live.remember) {
        m_live.apiKey = settings.value(QStringLiteral("apiKey")).toString();
        m_live.clearance = settings.value(QStringLiteral("clearance")).toString();
        m_live.userAgent = settings.value(QStringLiteral("userAgent")).toString();
        m_live.shuffleToken = settings.value(QStringLiteral("shuffleToken")).toString();
        m_live.goatedToken = settings.value(QStringLiteral("goatedToken")).toString();
        m_live.wolfbetToken = settings.value(QStringLiteral("wolfbetToken")).toString();
        m_live.correlationUserId = settings.value(QStringLiteral("correlationUserId")).toString();
        m_live.browserId = settings.value(QStringLiteral("browserId")).toString();
    }
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
    cfg.delayMs = settings.value(QStringLiteral("delayMs"), cfg.delayMs).toInt();
    settings.endGroup();
    m_autoPanel->setFrom(cfg);

    const bool dark = settings.value(QStringLiteral("General/darkTheme"), false).toBool();
    m_actDarkTheme->setChecked(dark);
    applyTheme(dark); // setChecked only fires toggled when the value changes
}

void MainWindow::saveSettings()
{
    saveConditions();
    QSettings settings;
	
    settings.setValue(QStringLiteral("MainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("MainWindow/state"), saveState());
    settings.setValue(QStringLiteral("MainWindow/centralSplitter"), m_centralSplitter->saveState());
    settings.setValue(QStringLiteral("MainWindow/mainSplitter"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("MainWindow/tableState2"),
                      m_betTable->horizontalHeader()->saveState());
    m_luaDock->saveSettings();
    settings.setValue(QStringLiteral("View/sidebar"), m_actShowSidebar->isChecked());
    settings.setValue(QStringLiteral("View/betList"), m_actShowBetList->isChecked());
    settings.setValue(QStringLiteral("View/bottomPanel"), m_actShowTabs->isChecked());

    settings.setValue(QStringLiteral("General/currency"), m_currencyTag);
    settings.setValue(QStringLiteral("General/startBalance"), m_startBalance);
    settings.setValue(QStringLiteral("General/clientSeed"), m_clientSeed);
    settings.setValue(QStringLiteral("General/randomClientSeed"), m_randomClientSeed);
    settings.setValue(QStringLiteral("General/game"), gameName(m_game));
    settings.setValue(QStringLiteral("General/darkTheme"), m_actDarkTheme->isChecked());

    m_currencyByCasino[int(m_live.casino)] = m_live.currency;
    settings.beginGroup(QStringLiteral("Live"));
    settings.setValue(QStringLiteral("casino"), m_live.casinoName());
    settings.setValue(QStringLiteral("site"), m_live.site);
    settings.setValue(QStringLiteral("shuffleSite"), m_live.shuffleSite);
    settings.setValue(QStringLiteral("extensionMode"), m_live.extensionMode);
    settings.setValue(QStringLiteral("currency"), m_currencyByCasino[0]);
    settings.setValue(QStringLiteral("shuffleCurrency"), m_currencyByCasino[1]);
    settings.setValue(QStringLiteral("goatedCurrency"), m_currencyByCasino[2]);
    settings.setValue(QStringLiteral("wolfbetCurrency"), m_currencyByCasino[3]);
    settings.setValue(QStringLiteral("remember"), m_live.remember);
	settings.setValue(QStringLiteral("enabled"), m_live.enabled);
	
    if (m_live.remember) {
        settings.setValue(QStringLiteral("apiKey"), m_live.apiKey);
        settings.setValue(QStringLiteral("clearance"), m_live.clearance);
        settings.setValue(QStringLiteral("userAgent"), m_live.userAgent);
        settings.setValue(QStringLiteral("shuffleToken"), m_live.shuffleToken);
        settings.setValue(QStringLiteral("goatedToken"), m_live.goatedToken);
        settings.setValue(QStringLiteral("wolfbetToken"), m_live.wolfbetToken);
        settings.setValue(QStringLiteral("correlationUserId"), m_live.correlationUserId);
        settings.setValue(QStringLiteral("browserId"), m_live.browserId);
    } else { // unticking "remember" wipes what was stored before
        settings.remove(QStringLiteral("apiKey"));
        settings.remove(QStringLiteral("clearance"));
        settings.remove(QStringLiteral("userAgent"));
        settings.remove(QStringLiteral("shuffleToken"));
        settings.remove(QStringLiteral("goatedToken"));
        settings.remove(QStringLiteral("wolfbetToken"));
        settings.remove(QStringLiteral("correlationUserId"));
        settings.remove(QStringLiteral("browserId"));
    }
    settings.endGroup();

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
    settings.setValue(QStringLiteral("delayMs"), cfg.delayMs);
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
    if (m_shuffle)
        m_shuffle->closeSocket();
    // ----------------------

    if (!m_discardSettings)
        saveSettings();
    event->accept();
}