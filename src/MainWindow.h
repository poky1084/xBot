#pragma once

#include "bot/WebViewTransport.h"
#include "ui/CookieGrabberDialog.h"

#include "bot/BotTypes.h"
#include "bot/LiveSettings.h"
#include "bot/ScriptStrategy.h"
#include "ui/ConditionBlock.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <functional>


class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QLineEdit;
class QSplitter;
class QTableView;
class QTabWidget;
class QTimer;

class AutoBetPanel;
class BalanceGraphWidget;
class BetHistoryModel;
class BotEngine;
class CookieTransport;
class ExtensionTransport;
class FairPanel;
class LogWidget;
class LuaEditorDock;
class ManualBetPanel;
class OptionsDialog;
class SimulatedProvider;
class StakeClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onRollOnce();
    void onStartAuto();
    void onStopAuto();
    void onPauseAuto();
    void onResetSession();
    void onClearHistory();
    void onRotateSeed();
    void rotateSeed(bool fromScript, const QString &clientSeed = QString());
    // Sets the configured client seed (rotating the seed pair), then runs `then`.
    // Runs `then` at once when no client seed is configured; in live mode it runs
    // after the server confirmed the rotation, and never if the rotation failed.
    void applyClientSeed(const std::function<void()> &then);
    void onEngineStarted();
    void onEngineStopped(const QString &reason);
    void onPausedChanged(bool paused);
    void onBetPlaced(const BetRecord &record);
    void onBetTableClicked(const QModelIndex &index);
    void onBetTableContextMenu(const QPoint &pos);
    void onLogMessage(const QString &message, bool isError);
    void onSearchEdited(const QString &text);
    void onSwitchToChart();
    void onTabChanged(int index);        // <-- NEW
    void onToggleDarkTheme(bool enabled);
    void onOpenOptions();
    void onOpenFile();
    void onOpenConditionBuilder();
    void onRunScript(const QString &script, ScriptLanguage language);
    void onAbout();
    void onGameSelected(Game game);

    // Live betting
    void onBalancesReady(const QMap<QString, double> &balances);
    void onBalancesFailed(const QString &error);
    void onExtensionReadyChanged(bool ready);
    void onBalanceTimer();

private:
    // Resolves a numeric bet id to its iid (BetLookup) and stores it in the model.
    // `done` (optional) runs afterwards with the iid, or "" if the lookup failed.
    void resolveBetIid(const QString &betId, std::function<void(const QString &)> done = {});
    QSet<QString> m_iidLookups; // bet ids with a lookup currently in flight
    void createActions();
    void createMenus();
    void createToolbar();
    void createSidebar();
    void createCentralWidget();
    void createStatusBar();
    void createLogDock();
    void createLuaDock();
    BotConfig currentConfig() const;
    void updateSessionWidgets();
    void updateRunTime();
    void startRunClock();
    void stopRunClock();
    void updateStatusWidgets();
    void updateFilterCounts();
    void updateChartLabels();
    void updateEmptyState();
    void applyTheme(bool dark);
    void loadSettings();
    void saveSettings();
    void loadConditions();
    void saveConditions();
	void updateConnectionStatus();
	
    // Live betting helpers
    bool configureConnection(const LiveSettings &settings, QString *errorOut = nullptr);
    void applyLiveSettings(const LiveSettings &settings);
    void resetSessionState();
    // Graph, run clock, speed and labels after the session statistics were reset.
    void refreshAfterStatsReset();
    void updateModeWidgets();
    // Sets the game being played (menu, Manual tab, window title). Used by the
    // Game menu and by Lua scripts that set game = "dice" / "limbo".
    void setGame(Game game);
    void updateWindowTitle();
    double displayBalance() const;
    QString currencyLabel() const;

    // Core
    BotEngine *m_engine = nullptr;
    Game m_game = Game::Dice;
    QActionGroup *m_gameGroup = nullptr;
    QAction *m_actGameDice = nullptr;
    QAction *m_actGameLimbo = nullptr;
    SimulatedProvider *m_provider = nullptr;
	
	bool m_isLoggedIn = false;

	
    // Live betting
    StakeClient *m_stake = nullptr;
    CookieTransport *m_cookie = nullptr;
    ExtensionTransport *m_ext = nullptr;
    QTimer *m_balanceTimer = nullptr;
    LiveSettings m_live;
    QPointer<OptionsDialog> m_optionsDialog;
    bool m_discardSettings = false; // "Clear data from PC" was used: don't save on exit
    QString m_lastBalanceError;

    // Bet list
    BetHistoryModel *m_model = nullptr;
    QTableView *m_betTable = nullptr;
    QLabel *m_emptyLabel = nullptr;
    // false once the user scrolls / clicks in the bet list: new bets then no
    // longer move the view. Scrolling back to the top resumes following.
    bool m_betFollow = true;

    // Sidebar
    QWidget *m_sidebar = nullptr;
    QLabel *m_lblBalance = nullptr;
    QLabel *m_lblProfit = nullptr;
    QLabel *m_lblWagered = nullptr;
    QLabel *m_lblWinRate = nullptr;
    QLabel *m_lblHighestBet = nullptr;
    QLabel *m_lblHighestProfit = nullptr;
    QLabel *m_lblLowestProfit = nullptr;
    QLabel *m_lblHighestStreak = nullptr;
    QLabel *m_lblLowestStreak = nullptr;
    QLabel *m_lblCurrentStreak = nullptr;
    QLabel *m_lblRunTime = nullptr;

    // Auto-bet running time (excludes paused periods)
    QTimer *m_runClock = nullptr;
    bool m_rotatingSeed = false;   // live seed rotation request in flight

    // Client seed for manual / auto bets (Options -> Game). Scripts never use it.
    QString m_clientSeed;
    bool m_randomClientSeed = false;
    bool m_clientSeedPending = true;               // manual bets: apply before the next roll
    std::function<void()> m_afterSeedRotation;     // continues once a live rotation is done
    QElapsedTimer m_runElapsed;
    qint64 m_runAccumMs = 0;

    // Splitters
    QSplitter *m_centralSplitter = nullptr;
    QSplitter *m_mainSplitter = nullptr;

    // Tabs
    QTabWidget *m_tabs = nullptr;
    ManualBetPanel *m_manualPanel = nullptr;
    AutoBetPanel *m_autoPanel = nullptr;
    BalanceGraphWidget *m_graph = nullptr;
    QLabel *m_chartMax = nullptr;
    QLabel *m_chartMin = nullptr;
    QLabel *m_chartNow = nullptr;
    FairPanel *m_fairPanel = nullptr;
    QLabel *m_logLineLabel = nullptr;

    // Log dock (hidden by default)
    LogWidget *m_logWidget = nullptr;
    QDockWidget *m_logDock = nullptr;

    // Strategy editor for Lua / JavaScript (dockable, hidden by default)
    LuaEditorDock *m_luaDock = nullptr;
    QAction *m_actLua = nullptr;      // "#" toolbar button / View menu entry
    bool m_luaRunning = false;        // the current auto-bet run is a script run

    // Toolbar / actions
    QAction *m_actRollOnce = nullptr;
    QAction *m_actStart = nullptr;
    QAction *m_actPause = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actClear = nullptr;
    QAction *m_actRotate = nullptr;
    QAction *m_actStats = nullptr;
    QAction *m_actReset = nullptr;
    QAction *m_actOptions = nullptr;
    QAction *m_actOpenFile = nullptr;
    QAction *m_actConditions = nullptr;
    QAction *m_actDarkTheme = nullptr;
    QAction *m_actShowLog = nullptr;
    QAction *m_actShowSidebar = nullptr;   // View > Sidebar (status filters + session stats)
    QAction *m_actShowBetList = nullptr;   // View > Bet list
    QAction *m_actShowTabs = nullptr;      // View > Bottom panel (Manual / Auto / Chart / Fair)
    QLineEdit *m_searchEdit = nullptr;

    // Status bar
    QLabel *m_statusStateIcon = nullptr;
    QLabel *m_statusStateText = nullptr;
    QLabel *m_statusBets = nullptr;
    QLabel *m_statusWagered = nullptr;
    QLabel *m_statusPl = nullptr;
    QLabel *m_statusSpeed = nullptr;
    QLabel *m_statusMode = nullptr;
    QLabel *m_statusBalance = nullptr;

	WebViewTransport *m_webTransport = nullptr;
	QPointer<CookieGrabberDialog> m_hiddenBrowser = nullptr;
    QString m_browserSite; // site/UA the hidden browser was created with
    QString m_browserUA;

    // Condition builder (per-game rules + named strategies)
    BlockList m_conditions;
    StrategyMap m_strategies;
    QString m_activeStrategy;
    QString m_selectedFile;

    QString m_currencyTag = QStringLiteral("USDT");
    double m_startBalance = BotConstants::kStartBalance;

    // Rolling betting-speed measurement (bets per second).
    QElapsedTimer m_speedTimer;
    qint64 m_speedBetsAtWindowStart = 0;
    double m_betsPerSecond = 0.0;
};