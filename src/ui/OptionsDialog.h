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
    void setLiveCurrencies(const QStringList &codes, const QString &selected);
    void setExtensionStatus(bool listening, bool connected);
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

private:
    void updateAuthFields();
    // The Casino selector changed: keep what was typed for the old casino and show the new one's.
    void onCasinoChanged(int index);
    // Adapts the "Live betting" box to the casino chosen in the Casino selector / menu
    // (Shuffle: Token, Direct mode only, USER_ID, BROWSER_ID;
    // Goated, Wolfbet: just the Token).
    void applyCasinoUi();
    void setFormRowVisible(QWidget *field, bool visible);
    void onClearData();
	void onGrabCookie();
    void onShuffleLogin();
    void onGoatedLogin();
    void onWolfbetLogin();
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
    QGroupBox *m_liveBox = nullptr;
    QFormLayout *m_liveForm = nullptr;
    QLineEdit *m_correlationEdit = nullptr;
    QLineEdit *m_browserIdEdit = nullptr;

    QCheckBox *m_liveEnable = nullptr;
    QComboBox *m_casinoCombo = nullptr;
    QString m_casinoCurrency[LiveSettings::kCasinoCount];
    QComboBox *m_authCombo = nullptr;
    QLineEdit *m_siteEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_clearanceEdit = nullptr;
    QLineEdit *m_userAgentEdit = nullptr;
    QLabel *m_extStatus = nullptr;
    QComboBox *m_liveCurrencyCombo = nullptr;
    QPushButton *m_loginBtn = nullptr;
	QPushButton *m_grabCookieBtn = nullptr;
    QPushButton *m_shuffleLoginBtn = nullptr;
    QPushButton *m_goatedLoginBtn = nullptr;
    QPushButton *m_wolfbetLoginBtn = nullptr;
    QLabel *m_loginStatus = nullptr;
    quint16 m_extensionPort = 4444;
};
