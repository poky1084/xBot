#pragma once

#include "bot/LiveSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = nullptr);

    bool darkTheme() const;
    void setDarkTheme(bool enabled);

    QString currencyTag() const;
    void setCurrencyTag(const QString &tag);

    double startBalance() const;
    void setStartBalance(double value);

    // Pause between bets in milliseconds (0 = none).
    int betDelayMs() const;
    void setBetDelayMs(int ms);

    // "Retry bet when bet higher than balance": keep waiting instead of halting when the next bet is
    // larger than the balance.
    bool retryNoBalance() const;
    void setRetryNoBalance(bool on);

    // Client seed used by manual and auto bets (Lua / JavaScript strategies ignore it).
    // "Random" = a fresh random 10-character seed instead of the typed one.
    QString clientSeed() const;
    void setClientSeed(const QString &seed);
    bool randomClientSeed() const;
    void setRandomClientSeed(bool random);

    // ---- Live betting
    LiveSettings liveSettings() const;
    void setLiveSettings(const LiveSettings &settings);
    void setLoginStatus(const QString &text, bool ok);
    // The first login input the selected casino needs that is still empty ("" = complete).
    QString missingLoginData() const;
    // "Browser extension": shows the login data read from the casino's tab (`grabbed.casino`).
    void setGrabbedCredentials(const LiveSettings &grabbed);
    // Shows which currencies the account holds (as a hint; the bet currency stays what the
    // user typed).
    void setLiveCurrencies(const QStringList &codes, const QString &selected);
    // `hosts` = sites of the casino tabs connected to the extension server.
    void setExtensionStatus(bool listening, const QStringList &hosts);
    // When on (default), the dialog logs in by itself (same as "Log in") as soon as every
    // input the selected casino needs is filled in, so its currencies load without a click.
    // Shuffle is skipped: its currency list is built in. Turn off while a bet is running.
    void setAutoFetchEnabled(bool on);
    // Bet currency remembered for each casino (indexed by int(LiveSettings::Casino)), so the
    // Casino selector can show the right one. Call before setLiveSettings(). The getter
    // includes the currency currently shown for the selected casino.
    void setCasinoCurrencies(const QStringList &codes);
    QStringList casinoCurrencies() const;

    // Deletes every value xBot stored on this PC (the whole registry key on
    // Windows, the settings file elsewhere). Returns false and fills `error` on failure.
    static bool clearStoredData(QString *error = nullptr);

signals:
    // The user pressed "Log in": apply the current live settings and fetch balances.
    void loginRequested();
    // All stored data was deleted; the app must exit without saving settings again.
    void dataCleared();
    // The user typed in "Site / mirror": liveSettings() holds the new site of the shown casino
    // (saved right away, not only when the dialog is accepted).
    void siteEdited();
    // An "Open … & Log in" window filled in new login data (liveSettings() of the shown casino):
    // the owner saves it at once, so it is kept even if the dialog is not closed with OK.
    void credentialsCaptured(const LiveSettings &settings);

private:
    void updateAuthFields();
    void updateExtensionStatusText();
    // The Casino selector changed: keep what was typed for the old casino and show the new one's.
    void onCasinoChanged(int index);
    // Adapts the "Live betting" box to the casino chosen in the Casino selector / menu
    // (Shuffle: Token, Direct mode only, USER_ID, BROWSER_ID;
    // Goated, Wolfbet, Paradice, Pasino: just the Token; Tower: Token, cf_clearance, User-Agent; Winna: Session, cf_clearance, User-Agent, Device ID; Duel: Token, UUID, cf_clearance;
    // Stake: API key, cf_clearance and the "Open Stake & Login" button).
    void applyCasinoUi();
    // The bet currency was typed / changed: fetch the balance for it (0 if the account
    // does not hold it), when the login data is complete.
    void onCurrencyEdited();
    // Fills the dropdown with the casino's valid currencies (cached list, plus what the
    // account holds), keeps the chosen currency, and fetches the list once per dialog.
    void updateCurrencyList();
    void setFormRowVisible(QWidget *field, bool visible);
    // Every input the selected casino needs is filled in and was not fetched with yet:
    // fetch the balances / currencies (emits loginRequested()). Shuffle is skipped.
    void maybeAutoFetch();
    // Restarts the short wait after typing / pasting before maybeAutoFetch() runs.
    void scheduleAutoFetch();
    // The login inputs of the selected casino, joined (what an auto fetch was made with).
    QString autoFetchKey() const;
    void onClearData();
	void onGrabCookie();
    // "Open … & Log in" clicked: empty every login input (token / API key, cookies, ids,
    // User-Agent) and the status, so the window's answer fills them in like fresh data.
    void clearLoginInputs();
    void onShuffleLogin();
    void onGoatedLogin();
    void onWolfbetLogin();
    void onParadiceLogin();
    void onPasinoLogin();
    void onTowerLogin();
    void onWinnaLogin();
    void onDuelLogin();
    void onStakeLogin();
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_currencyCombo = nullptr;
    QDoubleSpinBox *m_startBalanceSpin = nullptr;
    QLineEdit *m_clientSeedEdit = nullptr;
    QCheckBox *m_randomSeedCheck = nullptr;
    QSpinBox *m_betDelaySpin = nullptr;
    QCheckBox *m_retryNoBalanceCheck = nullptr;

    LiveSettings m_base;            // settings last handed to setLiveSettings()
    bool m_shuffle = false;
    bool m_goated = false;
    bool m_wolfbet = false;
    bool m_paradice = false;
    bool m_pasino = false;
    bool m_tower = false;
    bool m_winna = false;
    bool m_duel = false;
    QGroupBox *m_liveBox = nullptr;
    QFormLayout *m_liveForm = nullptr;
    QLineEdit *m_correlationEdit = nullptr;
    QLineEdit *m_browserIdEdit = nullptr;
    QLineEdit *m_duelUuidEdit = nullptr;       // Duel only
    QLineEdit *m_duelClearanceEdit = nullptr;  // Duel only
    QLineEdit *m_towerClearanceEdit = nullptr; // Tower / Winna: cf_clearance
    QLineEdit *m_towerUserAgentEdit = nullptr; // Tower / Winna: User-Agent
    QLineEdit *m_winnaDeviceEdit = nullptr;    // Winna only

    QCheckBox *m_liveEnable = nullptr;
    QComboBox *m_casinoCombo = nullptr;
    QString m_casinoCurrency[LiveSettings::kCasinoCount];
    QComboBox *m_authCombo = nullptr;
    QLineEdit *m_siteEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_clearanceEdit = nullptr;
    QLineEdit *m_userAgentEdit = nullptr;
    QLabel *m_extStatus = nullptr;
    QComboBox *m_liveCurrencyCombo = nullptr; // per casino: valid currencies, or typed
    QLabel *m_liveCurrencyLabel = nullptr;    // "Bet currency:"
    QStringList m_accountCurrencies;          // what the account holds (last login)
    bool m_currencyListFetched[LiveSettings::kCasinoCount] = {}; // fetched while open
    QString m_fetchedCurrency;               // currency the last balance fetch was made for
    QPushButton *m_loginBtn = nullptr; // always enabled
    bool m_loginBusy = false; // a login / balance fetch is running (blocks auto fetch)
	QPushButton *m_grabCookieBtn = nullptr;
    QPushButton *m_shuffleLoginBtn = nullptr;
    QPushButton *m_goatedLoginBtn = nullptr;
    QPushButton *m_wolfbetLoginBtn = nullptr;
    QPushButton *m_paradiceLoginBtn = nullptr;
    QPushButton *m_pasinoLoginBtn = nullptr;
    QPushButton *m_towerLoginBtn = nullptr;
    QPushButton *m_winnaLoginBtn = nullptr;
    QPushButton *m_duelLoginBtn = nullptr;
    QPushButton *m_stakeLoginBtn = nullptr;
    QLabel *m_loginStatus = nullptr;
    quint16 m_extensionPort = 4444;
    bool m_extConnected = false;      // the shown casino's tab is connected
    bool m_extListening = false;
    QStringList m_extHosts;           // sites of the connected tabs
    bool m_autoFetchEnabled = true;
    QTimer *m_autoFetchTimer = nullptr;
    QString m_autoFetchedKey[LiveSettings::kCasinoCount]; // inputs last auto-fetched with
};
