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
class QSettings;
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
class GoatedTransport;
class WolfbetTransport;
class ParadiceTransport;
class PasinoTransport;
class TowerTransport;
class WinnaTransport;
class DuelTransport;
class DuelBrowser;
class ShuffleTransport;
class SimulatedProvider;
class StakeClient;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // slot: this window's number; its data is saved in QSettings group "Windows/<slot>"
    // (see ui/WindowSettings.h). 0 = the main window, 1 = the first "+" window, …
    // freshWebSession = true: its WebView2 windows start from an empty browser profile.
    explicit MainWindow(int slot = 0, bool freshWebSession = false, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Once per start, before the first window: settings saved by older versions (shared by
    // all windows) become the main window's (slot 0) data.
    static void migrateLegacySettings();

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
    void onCasinoSelected(LiveSettings::Casino casino);

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
    BotConfig currentConfig() const; // auto-bet: Auto Bet tab settings (side from its Direction box)
    // What Start hands the engine: condition mode = Manual tab + the Auto Bet tab's base bet
    // and odds / direction; otherwise currentConfig().
    BotConfig autoRunConfig(bool condMode) const;
    // Auto Bet tab edited: passes the settings to a running (or starting) non-script auto-bet.
    void onAutoSettingsEdited();
    BotConfig manualConfig() const;  // manual roll: Manual Bet tab only
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
    QString settingsGroup() const; // "Windows/<slot>"
    static QList<int> &openSlots(); // slots of the windows open right now
    static int freeSlot();          // lowest slot that is not open
    // Copies a window's settings group; keys starting with one of `skip` are left out.
    static void copySettingsGroup(const QString &from, const QString &to,
                                  const QStringList &skip = QStringList());
    void saveOptionsSettings(QSettings &settings);
    // Manual Bet: dice target + side and limbo target (= Multiplier / Win chance).
    void saveManualTargets(QSettings &settings);
    void loadConditions();
    void saveConditions();
	void updateConnectionStatus();
    void updateCasinoMenu();
    // Shows only the games the selected casino offers; switches to dice when the current game
    // is not one of them.
    void updateGameMenu();
    // Tells the Auto Bet tab which casino's win chance to show (house edge) and the game.
    void updateAutoOdds();
    void updateFairTab();
    bool betListStacked() const;
    int bottomPanelHeight() const;
    void updateCentralHeight();
    void positionBottomPanel();
    void fitTopDock(bool growPanel = false);
    // Game switched: the Manual / Auto Bet panel gets the full height of its new content (from
    // the panel docked above it, or else from the one docked below it).
    void fitPanelToGame();
    void rememberLogHeight();
    void keepLogHeight();
    void scheduleKeepLogHeight();
    void setupFloatingDocks();
    void detachFloatingDock(QDockWidget *dock, bool bringToFront = false);
    static QString casinoDisplayName(LiveSettings::Casino casino);
    QAction *casinoAction(LiveSettings::Casino casino) const;
    void closeHiddenBrowser();
    void ensureHiddenBrowser(const LiveSettings &s);

    // Shuffle balance cache: kept current by Shuffle's own websocket, whichever casino is active.
    void onShuffleBalancesUpdated(const QMap<QString, double> &balances);
    void onShuffleBackgroundError(const QString &error);
    void updateShuffleBackground(const LiveSettings &s, bool closeIfOff = true); // run / stop the background socket
    QString shuffleCurrency() const;                     // bet currency of Shuffle (active or not)
    void refreshShuffleBalanceVar();
    bool loadCachedShuffleBalance();                     // instant load into the session stats

    // Goated / Stake balance cache, same idea as Shuffle's: the last known balance of each casino
    // is kept in a variable while another casino is active, so switching back shows it at once.
    void onGoatedBalancesUpdated(const QMap<QString, double> &balances);
    QString goatedCurrency() const;                      // bet currency of Goated (active or not)
    QString stakeCurrency() const;                       // bet currency of Stake (active or not)
    void refreshGoatedBalanceVar();
    void refreshStakeBalanceVar();
    void snapshotLeavingCasinoBalance();                 // remember the engine balance of the casino being left
    bool loadCachedCasinoBalance();                      // instant load (Goated / Stake) into the session stats
    void applyGoatedCredentials(const LiveSettings &s);  // Goated token for the direct transport (background reads)
    // Wolfbet balance cache (plain HTTPS, same idea as Goated's)
    void onWolfbetBalancesUpdated(const QMap<QString, double> &balances);
    QString wolfbetCurrency() const;                     // bet currency of Wolfbet (active or not)
    void refreshWolfbetBalanceVar();
    void applyWolfbetCredentials(const LiveSettings &s); // Wolfbet token for the direct transport (background reads)
    // Paradice balance cache (plain HTTPS, same idea as Goated's)
    void onParadiceBalancesUpdated(const QMap<QString, double> &balances);
    QString paradiceCurrency() const;                    // bet currency of Paradice (active or not)
    void refreshParadiceBalanceVar();
    void applyParadiceCredentials(const LiveSettings &s); // Paradice token for the direct transport (background reads)
    // Pasino balance cache (plain HTTPS, same idea as Goated's)
    void onPasinoBalancesUpdated(const QMap<QString, double> &balances);
    QString pasinoCurrency() const;                      // bet currency of Pasino (active or not)
    void refreshPasinoBalanceVar();
    void applyPasinoCredentials(const LiveSettings &s);  // Pasino token for the direct transport (background reads)
    // Tower balance cache (plain HTTPS, same idea as Goated's)
    void onTowerBalancesUpdated(const QMap<QString, double> &balances);
    QString towerCurrency() const;                       // bet currency of Tower (active or not)
    void refreshTowerBalanceVar();
    void applyTowerCredentials(const LiveSettings &s);   // Tower token / cf_clearance for the direct transport
    // Winna balance cache (plain HTTPS, same idea as Goated's)
    void onWinnaBalancesUpdated(const QMap<QString, double> &balances);
    QString winnaCurrency() const;                       // bet currency of Winna (active or not)
    void refreshWinnaBalanceVar();
    void applyWinnaCredentials(const LiveSettings &s);   // Winna session / cf_clearance for the direct transport
    // Duel balance cache (plain HTTPS, same idea as Goated's)
    void onDuelBalancesUpdated(const QMap<QString, double> &balances);
    QString duelCurrency() const;                        // bet currency of Duel (active or not)
    void refreshDuelBalanceVar();
    void applyDuelCredentials(const LiveSettings &s);    // Duel cookie / UUID for the direct transport (background reads)
    void ensureDuelBrowser();                            // hidden WebView2 on duel.com (Duel's requests go through it)
    // Stake's balance is read through its extension / hidden browser while Shuffle or Goated is
    // the active casino, so the Stake cache is always current.
    void onStakeBackgroundBalances(const QMap<QString, double> &balances);
    IStakeTransport *stakeSessionTransport() const;      // extension or hidden browser, per login method
    void requestStakeBackgroundBalance();
    // Reads the balance of every casino that is not the active one (Shuffle socket, Goated HTTPS,
    // Stake extension / browser), so each cache is current when switching casino, e.g. from a script.
    void refreshBackgroundBalances();
    // Balance request of the active casino, skipped when one was sent less than `minGapMs` ago
    // (quick casino switches from a script would otherwise trip the sites' rate limits).
    void fetchActiveBalance(int minGapMs);
    void keepStakeSessionAlive(const LiveSettings &s);   // extension server / hidden browser stay up
	
    // Live betting helpers
    bool configureConnection(const LiveSettings &settings, QString *errorOut = nullptr);
    bool configureTransports(const LiveSettings &settings, QString *errorOut);
    // Starts / stops the extension server and points every casino's transport at it (or not).
    bool updateExtensionServer(const LiveSettings &s, QString *errorOut);
    // "Browser extension": the login data (API key / tokens / ids) is read from the casino's tab.
    using GrabDone = std::function<void(const LiveSettings &grabbed, const QString &error)>;
    void grabExtensionCredentials(LiveSettings::Casino c, const LiveSettings &base, GrabDone done);
    QStringList extensionTabHosts(const LiveSettings &s, LiveSettings::Casino c) const;
    // Takes casino `c`'s login data from `grabbed` into m_live (reconfigures + saves when it changed).
    void applyGrabbedCredentials(LiveSettings::Casino c, const LiveSettings &grabbed);
    // Reads casino `c`'s login from its tab, then fetches its balances (active / background).
    void refreshExtensionLogin(LiveSettings::Casino c);
    void loginFromDialog(OptionsDialog &dialog); // "Log in" in Options once the inputs are set
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
    // `currency = "btc"` in a Lua / JavaScript strategy
    // `casino = "shuffle"` in a Lua / JavaScript strategy (keeps the session)
    bool switchCasinoFromScript(const QString &casino, QString &error);
    bool switchCurrencyFromScript(const QString &currency, double &balance, QString &error);

    // Core
    BotEngine *m_engine = nullptr;
    Game m_game = Game::Dice;
    QActionGroup *m_gameGroup = nullptr;
    // Game menu: one action per game; only the selected casino's games are shown.
    QMap<Game, QAction *> m_gameActions;
    QAction *m_gameSeparator = nullptr; // between dice / limbo and Stake's other games
    SimulatedProvider *m_provider = nullptr;
	
	bool m_isLoggedIn = false;
    bool m_extActiveConnected = false; // the active casino's extension tab is connected
    QElapsedTimer m_extGrabClock;      // throttles re-reading the login while not logged in

	
    // Live betting
    StakeClient *m_stake = nullptr;
    CookieTransport *m_cookie = nullptr;
    ExtensionTransport *m_ext = nullptr;
    ShuffleTransport *m_shuffle = nullptr;   // Shuffle "Direct mode" (no WebView2 / extension)
    GoatedTransport *m_goated = nullptr;     // Goated "Direct mode" (plain HTTPS, token only)
    WolfbetTransport *m_wolfbet = nullptr;   // Wolfbet "Direct mode" (plain HTTPS, token only)
    ParadiceTransport *m_paradice = nullptr; // Paradice "Direct mode" (plain HTTPS GraphQL, token only)
    PasinoTransport *m_pasino = nullptr;     // Pasino "Direct mode" (plain HTTPS, token only)
    TowerTransport *m_tower = nullptr;       // Tower.bet "Direct mode" (HTTPS, token + cf_clearance)
    WinnaTransport *m_winna = nullptr;       // Winna.com "Direct mode" (HTTPS, session + cf_clearance)
    DuelTransport *m_duel = nullptr;         // Duel (requests run in the hidden Duel browser)
    DuelBrowser *m_duelBrowser = nullptr;    // hidden WebView2 on duel.com, created on first use
    QActionGroup *m_casinoGroup = nullptr;   // Casino menu: Stake / Shuffle / Goated / Wolfbet / Duel / Paradice / Pasino / Tower / Winna
    QAction *m_actCasinoStake = nullptr;
    QAction *m_actCasinoShuffle = nullptr;
    QAction *m_actCasinoGoated = nullptr;
    QAction *m_actCasinoWolfbet = nullptr;
    QAction *m_actCasinoDuel = nullptr;
    QAction *m_actCasinoParadice = nullptr;
    QAction *m_actCasinoPasino = nullptr;
    QAction *m_actCasinoTower = nullptr;
    QAction *m_actCasinoWinna = nullptr;
    // bet currency remembered per casino, indexed by int(LiveSettings::Casino)
    QString m_currencyByCasino[LiveSettings::kCasinoCount];
    QTimer *m_balanceTimer = nullptr;
    QElapsedTimer m_bgBalanceClock; // last background balance read of the other casinos
    QElapsedTimer m_activeFetchClock[LiveSettings::kCasinoCount]; // last active-casino request, per casino
    LiveSettings m_live;
    QPointer<OptionsDialog> m_optionsDialog;
    bool m_manualTargetsLoaded = false; // Manual Bet targets restored: changes may be saved
    bool m_discardSettings = false; // "Clear data from PC" was used: don't save on exit
    int m_slot = 0;                 // settings slot, see settingsGroup()
    QString m_lastBalanceError;

    // Bet list
    BetHistoryModel *m_model = nullptr;
    QTableView *m_betTable = nullptr;
    QLabel *m_emptyLabel = nullptr;
    // false once the user scrolls / clicks in the bet list: new bets then no
    // longer move the view. Scrolling back to the top resumes following.
    bool m_betFollow = true;
    bool m_betClickWasSelected = false; // row under the mouse was already selected at press

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
    QDockWidget *m_betListDock = nullptr; // holds m_betTable
    QWidget *m_centralArea = nullptr;     // central area; the bottom panel sits at its bottom
    int m_panelHeight = 0;                // the bottom panel's content height
    int m_logHeight = -1;                 // the Log height the user chose (kept, see keepLogHeight)
    bool m_separatorDrag = false;         // mouse held on the main window: a separator drag
    bool m_dragCapped = false;            // bottom panel capped during that drag

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

    // Status + Session sidebar (dockable like the Log; docked on the left by default)
    QDockWidget *m_sidebarDock = nullptr;

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
    QMap<QString, double> m_balances; // last balances fetched from the server (lowercase code -> amount)

    // Shuffle's current balance, updated on every BalanceUpdated push even while Stake is the
    // active casino. m_shuffleBalance is the one of the Shuffle bet currency; the map holds all.
    QMap<QString, double> m_shuffleBalances;
    double m_shuffleBalance = 0.0;
    bool m_shuffleBalanceKnown = false;

    // Same for Goated and Stake: last known balances (lowercase code -> amount) of each casino,
    // and the balance of its bet currency in m_goatedBalance / m_stakeBalance.
    QMap<QString, double> m_goatedBalances;
    double m_goatedBalance = 0.0;
    bool m_goatedBalanceKnown = false;
    QMap<QString, double> m_wolfbetBalances;
    double m_wolfbetBalance = 0.0;
    bool m_wolfbetBalanceKnown = false;
    QMap<QString, double> m_paradiceBalances;
    double m_paradiceBalance = 0.0;
    bool m_paradiceBalanceKnown = false;
    QMap<QString, double> m_pasinoBalances;
    double m_pasinoBalance = 0.0;
    bool m_pasinoBalanceKnown = false;
    QMap<QString, double> m_towerBalances;
    double m_towerBalance = 0.0;
    bool m_towerBalanceKnown = false;
    QMap<QString, double> m_winnaBalances;
    double m_winnaBalance = 0.0;
    bool m_winnaBalanceKnown = false;
    QMap<QString, double> m_duelBalances;
    double m_duelBalance = 0.0;
    bool m_duelBalanceKnown = false;
    QMap<QString, double> m_stakeBalances;
    double m_stakeBalance = 0.0;
    bool m_stakeBalanceKnown = false;
    QString m_lastShuffleBgError;
    bool m_retryNoBalance = false; // Options -> Game -> Retry bet when bet higher than balance
    int m_betDelayMs = 0; // Options -> Game -> Delay (ms between bets)
    double m_startBalance = BotConstants::kStartBalance;

    // Rolling betting-speed measurement (bets per second).
    QElapsedTimer m_speedTimer;
    qint64 m_speedBetsAtWindowStart = 0;
    double m_betsPerSecond = 0.0;
};