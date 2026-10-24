#pragma once

#include "bot/LiveSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

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
    void onClearData();
	void onGrabCookie();
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_currencyCombo = nullptr;
    QDoubleSpinBox *m_startBalanceSpin = nullptr;
    QLineEdit *m_clientSeedEdit = nullptr;
    QCheckBox *m_randomSeedCheck = nullptr;

    QCheckBox *m_liveEnable = nullptr;
    QComboBox *m_authCombo = nullptr;
    QLineEdit *m_siteEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_clearanceEdit = nullptr;
    QLineEdit *m_userAgentEdit = nullptr;
    QLabel *m_extStatus = nullptr;
    QComboBox *m_liveCurrencyCombo = nullptr;
    QPushButton *m_loginBtn = nullptr;
	QPushButton *m_grabCookieBtn = nullptr;
    QLabel *m_loginStatus = nullptr;
    QCheckBox *m_rememberCheck = nullptr;
    quint16 m_extensionPort = 4444;
};
