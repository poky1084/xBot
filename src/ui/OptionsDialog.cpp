#include "ui/CookieGrabberDialog.h"
#include "ui/GoatedLoginDialog.h"
#include "ui/OptionsDialog.h"
#include "ui/ShuffleLoginDialog.h"
#include "ui/WolfbetLoginDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QMessageBox>
#include <QSettings>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#endif


OptionsDialog::OptionsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Options — xBot"));
    setMinimumWidth(480);

    auto *root = new QVBoxLayout(this);

    auto *appearanceBox = new QGroupBox(tr("Appearance"), this);
    auto *appearanceForm = new QFormLayout(appearanceBox);
    appearanceForm->setContentsMargins(8, 10, 8, 8);

    m_themeCombo = new QComboBox;
    m_themeCombo->addItem(tr("Light (qBittorrent look)"));
    m_themeCombo->addItem(tr("Dark"));
    appearanceForm->addRow(tr("Theme:"), m_themeCombo);

    m_currencyCombo = new QComboBox;
    m_currencyCombo->setEditable(true);
    m_currencyCombo->addItems({QStringLiteral("USDT"), QStringLiteral("BTC"),
                               QStringLiteral("ETH"), QStringLiteral("LTC"),
                               QStringLiteral("DOGE"), QStringLiteral("TRX")});
    appearanceForm->addRow(tr("Currency label:"), m_currencyCombo);

    root->addWidget(appearanceBox);

    auto *gameBox = new QGroupBox(tr("Game"), this);
    auto *gameForm = new QFormLayout(gameBox);
    gameForm->setContentsMargins(8, 10, 8, 8);

    m_startBalanceSpin = new QDoubleSpinBox;
    m_startBalanceSpin->setRange(1.0, 1e9);
    m_startBalanceSpin->setDecimals(2);
    m_startBalanceSpin->setSingleStep(100.0);
    m_startBalanceSpin->setSuffix(QStringLiteral(" USDT"));
    m_startBalanceSpin->setToolTip(tr("Play-money balance granted on session reset"));
    gameForm->addRow(tr("Simulation balance:"), m_startBalanceSpin);

    m_clientSeedEdit = new QLineEdit;
    m_clientSeedEdit->setMaxLength(255);
    m_clientSeedEdit->setPlaceholderText(tr("empty = keep the current client seed"));
    m_clientSeedEdit->setToolTip(
        tr("Client seed for manual and auto bets. It is set by rotating the seed pair "
           "before an auto-bet starts (and before the first manual bet after changing "
           "this option). Lua / JavaScript strategies do not use it."));
    gameForm->addRow(tr("Client seed:"), m_clientSeedEdit);

    m_randomSeedCheck = new QCheckBox(tr("Random client seed (10 characters)"));
    m_randomSeedCheck->setToolTip(
        tr("Use a fresh random alphanumeric client seed each time instead of the one typed above."));
    connect(m_randomSeedCheck, &QCheckBox::toggled, this,
            [this](bool random) { m_clientSeedEdit->setEnabled(!random); });
    gameForm->addRow(QString(), m_randomSeedCheck);

    m_betDelaySpin = new QSpinBox;
    m_betDelaySpin->setRange(0, 3600000);
    m_betDelaySpin->setSingleStep(10);
    m_betDelaySpin->setValue(0);
    m_betDelaySpin->setSuffix(QStringLiteral(" ms"));
    m_betDelaySpin->setToolTip(tr("Pause between bets in milliseconds (0 = as fast as possible). "
                                  "Applies to auto-bet and scripts; with the auto-bet panel's own "
                                  "delay the longer one is used."));
    gameForm->addRow(tr("Delay between bet:"), m_betDelaySpin);

    auto *note = new QLabel(
        tr(""),
        gameBox);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#737373;"));
    gameForm->addRow({}, note);

    root->addWidget(gameBox);

    // ------------------------------------------------------------- live betting
    auto *liveBox = new QGroupBox(tr("Live betting"), this);
    m_liveBox = liveBox;
    auto *liveForm = new QFormLayout(liveBox);
    m_liveForm = liveForm;
    liveForm->setContentsMargins(8, 10, 8, 8);

    m_liveEnable = new QCheckBox(tr("Enable live betting"));
    liveForm->addRow(m_liveEnable);

    m_authCombo = new QComboBox;
    m_authCombo->addItem(tr("Cookie (cf_clearance)"));
    m_authCombo->addItem(tr("Browser extension"));
    liveForm->addRow(tr("Login method:"), m_authCombo);

    m_siteEdit = new QLineEdit;
    m_siteEdit->setPlaceholderText(QStringLiteral("stake.com"));
    liveForm->addRow(tr("Site / mirror:"), m_siteEdit);

    m_apiKeyEdit = new QLineEdit;
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("x-access-token (API key)"));
    liveForm->addRow(tr("API key:"), m_apiKeyEdit);

    // Shuffle only (hidden for Stake)
    m_correlationEdit = new QLineEdit;
	m_correlationEdit->setEchoMode(QLineEdit::Password);
    m_correlationEdit->setPlaceholderText(tr("user id used in the x-correlation-id header"));
    liveForm->addRow(tr("CORRELATION_USER_ID:"), m_correlationEdit);

    m_browserIdEdit = new QLineEdit;
	m_browserIdEdit->setEchoMode(QLineEdit::Password);
    m_browserIdEdit->setPlaceholderText(tr("browser id used in the x-correlation-id header"));
    liveForm->addRow(tr("BROWSER_ID:"), m_browserIdEdit);

    // Shuffle only: opens the site, waits for the login, then fills Token / IDs.
    m_shuffleLoginBtn = new QPushButton(tr("Open Shuffle && Log in - to autofill data"));
    m_shuffleLoginBtn->setToolTip(
        tr("Opens the site in a browser window. After you log in, Token, CORRELATION_USER_ID "
           "and BROWSER_ID are filled in automatically."));
    m_shuffleLoginBtn->setEnabled(ShuffleLoginDialog::isAvailable());
    liveForm->addRow(QString(), m_shuffleLoginBtn);
    connect(m_shuffleLoginBtn, &QPushButton::clicked, this, &OptionsDialog::onShuffleLogin);

    // Goated only: opens the site, waits for the login, then fills the Token.
    m_goatedLoginBtn = new QPushButton(tr("Open Goated && Log in - to autofill token"));
    m_goatedLoginBtn->setToolTip(
        tr("Opens the site in a browser window. After you log in, the Token is filled in "
           "automatically."));
    m_goatedLoginBtn->setEnabled(GoatedLoginDialog::isAvailable());
    liveForm->addRow(QString(), m_goatedLoginBtn);
    connect(m_goatedLoginBtn, &QPushButton::clicked, this, &OptionsDialog::onGoatedLogin);

    // Wolfbet only: opens the site, waits for the login, then fills the Token.
    m_wolfbetLoginBtn = new QPushButton(tr("Open Wolfbet && Log in - to autofill token"));
    m_wolfbetLoginBtn->setToolTip(
        tr("Opens the site in a browser window. After you log in, the Token is filled in "
           "automatically."));
    m_wolfbetLoginBtn->setEnabled(WolfbetLoginDialog::isAvailable());
    liveForm->addRow(QString(), m_wolfbetLoginBtn);
    connect(m_wolfbetLoginBtn, &QPushButton::clicked, this, &OptionsDialog::onWolfbetLogin);

    m_clearanceEdit = new QLineEdit;
    m_clearanceEdit->setEchoMode(QLineEdit::Password);
    m_clearanceEdit->setPlaceholderText(tr("cf_clearance cookie value"));
    //liveForm->addRow(tr("cf_clearance:"), m_clearanceEdit);

	m_grabCookieBtn = new QPushButton(tr("Open site && grab cf_clearance…"));
	m_grabCookieBtn->setEnabled(CookieGrabberDialog::isAvailable());
	//liveForm->addRow({}, m_grabCookieBtn);
	connect(m_grabCookieBtn, &QPushButton::clicked, this, &OptionsDialog::onGrabCookie);

    m_userAgentEdit = new QLineEdit;
    m_userAgentEdit->setPlaceholderText(tr("User-Agent of the browser that got the cookie"));
    //liveForm->addRow(tr("User-Agent:"), m_userAgentEdit);

    m_extStatus = new QLabel;
    m_extStatus->setWordWrap(true);
    liveForm->addRow(tr("Extension:"), m_extStatus);

    m_liveCurrencyCombo = new QComboBox;
    m_liveCurrencyCombo->setEditable(true);
    m_liveCurrencyCombo->setToolTip(tr("Currency to bet with (filled in after logging in)"));
    liveForm->addRow(tr("Bet currency:"), m_liveCurrencyCombo);

    auto *loginRow = new QWidget;
    auto *loginLayout = new QHBoxLayout(loginRow);
    loginLayout->setContentsMargins(0, 0, 0, 0);
    m_loginBtn = new QPushButton(tr("Log in"));
    m_loginStatus = new QLabel;
    m_loginStatus->setWordWrap(true);
    loginLayout->addWidget(m_loginBtn);
    loginLayout->addWidget(m_loginStatus, 1);
    liveForm->addRow(loginRow);

    m_rememberCheck = new QCheckBox(tr("Remember API key and cookie on this computer"));
    liveForm->addRow(m_rememberCheck);

    connect(m_authCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { updateAuthFields(); });
    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        m_loginBtn->setEnabled(false);
        setLoginStatus(tr("Logging in…"), true);
        emit loginRequested();
    });

    root->addWidget(liveBox);
    applyCasinoUi();
    updateAuthFields();
    setExtensionStatus(false, false);

    // ------------------------------------------------------------------- data
    auto *dataBox = new QGroupBox(tr("Data"), this);
    auto *dataLayout = new QHBoxLayout(dataBox);
    dataLayout->setContentsMargins(8, 10, 8, 8);
    auto *clearBtn = new QPushButton(tr("Clear data from PC"));
    clearBtn->setToolTip(tr("Deletes everything xBot saved on this computer (registry): "
                            "settings, window layout, saved API key / cookie, Lua and JavaScript scripts."));
    auto *clearNote = new QLabel(tr("Removes all saved xBot settings from this PC."));
    clearNote->setWordWrap(true);
    clearNote->setStyleSheet(QStringLiteral("color:#737373;"));
    dataLayout->addWidget(clearBtn);
    dataLayout->addWidget(clearNote, 1);
    connect(clearBtn, &QPushButton::clicked, this, &OptionsDialog::onClearData);
    root->addWidget(dataBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

bool OptionsDialog::clearStoredData(QString *error)
{
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();

    {
        QSettings settings;
        settings.clear();
        settings.sync();
    }

#ifdef Q_OS_WIN
    // QSettings::clear() empties the key but leaves it behind; remove the whole
    // HKEY_CURRENT_USER\Software\<org>\<app> tree, then the <org> key if it is empty.
    const QString appKey = QStringLiteral("Software\\%1\\%2").arg(org, app);
    const QString orgKey = QStringLiteral("Software\\%1").arg(org);
    const LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(appKey.utf16()));
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
        if (error)
            *error = tr("Could not delete the registry key HKEY_CURRENT_USER\\%1 (error %2).")
                         .arg(appKey)
                         .arg(rc);
        return false;
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(orgKey.utf16())); // only if empty
#else
    Q_UNUSED(org);
    Q_UNUSED(app);
#endif
    return true;
}

void OptionsDialog::onClearData()
{
    const auto answer = QMessageBox::warning(
        this, tr("Clear data from PC"),
        tr("This permanently deletes everything xBot saved on this computer: settings, window "
           "layout, the remembered API key and cookie, and the Lua and JavaScript scripts.\n\n"
           "xBot will close afterwards. Continue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString err;
    if (!clearStoredData(&err)) {
        QMessageBox::critical(this, tr("Clear data from PC"), err);
        return;
    }
    emit dataCleared();
}

bool OptionsDialog::darkTheme() const
{
    return m_themeCombo->currentIndex() == 1;
}

void OptionsDialog::setDarkTheme(bool enabled)
{
    m_themeCombo->setCurrentIndex(enabled ? 1 : 0);
}

QString OptionsDialog::currencyTag() const
{
    return m_currencyCombo->currentText().trimmed();
}

void OptionsDialog::setCurrencyTag(const QString &tag)
{
    m_currencyCombo->setEditText(tag);
}

double OptionsDialog::startBalance() const
{
    return m_startBalanceSpin->value();
}

QString OptionsDialog::clientSeed() const
{
    return m_clientSeedEdit->text().trimmed();
}

void OptionsDialog::setClientSeed(const QString &seed)
{
    m_clientSeedEdit->setText(seed);
}

bool OptionsDialog::randomClientSeed() const
{
    return m_randomSeedCheck->isChecked();
}

void OptionsDialog::setRandomClientSeed(bool random)
{
    m_randomSeedCheck->setChecked(random);
    m_clientSeedEdit->setEnabled(!random);
}

int OptionsDialog::betDelayMs() const
{
    return m_betDelaySpin->value();
}

void OptionsDialog::setBetDelayMs(int ms)
{
    m_betDelaySpin->setValue(ms);
}

void OptionsDialog::setStartBalance(double value)
{
    m_startBalanceSpin->setValue(value);
}

// ------------------------------------------------------------------ live

void OptionsDialog::setFormRowVisible(QWidget *field, bool visible)
{
    field->setVisible(visible);
    if (QWidget *label = m_liveForm->labelForField(field))
        label->setVisible(visible);
}

void OptionsDialog::applyCasinoUi()
{
    const bool shuffle = m_shuffle;
    const bool goated = m_goated;
    const bool wolfbet = m_wolfbet;
    const bool tokenOnly = goated || wolfbet;        // the token is the only input
    const bool direct = shuffle || tokenOnly; // all use "Direct mode" (plain HTTPS)

    m_liveBox->setTitle(wolfbet ? tr("Live betting — Wolfbet")
                                : goated ? tr("Live betting — Goated")
                                         : shuffle ? tr("Live betting — Shuffle") : tr("Live betting"));

    // Login method: Shuffle, Goated and Wolfbet have the one "Direct mode" (no WebView2, no browser
    // extension).
    {
        const QSignalBlocker blocker(m_authCombo);
        m_authCombo->clear();
        if (direct) {
            m_authCombo->addItem(tr("Direct mode"));
        } else {
            m_authCombo->addItem(tr("Cookie (cf_clearance)"));
            m_authCombo->addItem(tr("Browser extension"));
        }
        m_authCombo->setCurrentIndex(0);
    }

    // "API key" is the "Token" on Shuffle, Goated and Wolfbet.
    if (auto *label = qobject_cast<QLabel *>(m_liveForm->labelForField(m_apiKeyEdit)))
        label->setText(direct ? tr("Token:") : tr("API key:"));
    m_apiKeyEdit->setPlaceholderText(wolfbet ? tr("\"token\" cookie (value / JWT)")
                                             : goated ? tr("g3_token (JWT)")
                                                      : shuffle ? tr("authorization token (JWT)")
                                                                : tr("x-access-token (API key)"));
    m_siteEdit->setPlaceholderText(shuffle ? QStringLiteral("shuffle.com")
                                           : QStringLiteral("stake.com"));
    m_rememberCheck->setText(tokenOnly ? tr("Remember token on this computer")
                                    : shuffle ? tr("Remember token and IDs on this computer")
                                              : tr("Remember API key and cookie on this computer"));

    setFormRowVisible(m_siteEdit, !tokenOnly);     // Goated / Wolfbet: the token is the only input
    setFormRowVisible(m_correlationEdit, shuffle);
    setFormRowVisible(m_browserIdEdit, shuffle);
    setFormRowVisible(m_shuffleLoginBtn, shuffle);
    setFormRowVisible(m_goatedLoginBtn, goated);
    setFormRowVisible(m_wolfbetLoginBtn, wolfbet);
    setFormRowVisible(m_extStatus, !direct);
}

void OptionsDialog::updateAuthFields()
{
    const bool ext = !m_shuffle && !m_goated && !m_wolfbet && m_authCombo->currentIndex() == 1;
    m_clearanceEdit->setEnabled(!ext);
    m_userAgentEdit->setEnabled(!ext);
    m_extStatus->setEnabled(ext);
    if (m_grabCookieBtn)
        m_grabCookieBtn->setEnabled(!ext && !m_shuffle && !m_goated && !m_wolfbet && CookieGrabberDialog::isAvailable());
}

LiveSettings OptionsDialog::liveSettings() const
{
    // Start from what we were given so the other casino's values are kept.
    LiveSettings s = m_base;
    s.enabled = m_liveEnable->isChecked();
    s.casino = m_wolfbet ? LiveSettings::Casino::Wolfbet
               : m_goated ? LiveSettings::Casino::Goated
                          : m_shuffle ? LiveSettings::Casino::Shuffle : LiveSettings::Casino::Stake;
    if (m_wolfbet) {
        s.wolfbetToken = m_apiKeyEdit->text().trimmed();
    } else if (m_goated) {
        s.goatedToken = m_apiKeyEdit->text().trimmed();
    } else if (m_shuffle) {
        const QString site = m_siteEdit->text().trimmed();
        s.shuffleSite = site.isEmpty() ? QStringLiteral("shuffle.com") : site;
        s.shuffleToken = m_apiKeyEdit->text().trimmed();
        s.correlationUserId = m_correlationEdit->text().trimmed();
        s.browserId = m_browserIdEdit->text().trimmed();
    } else {
        s.extensionMode = m_authCombo->currentIndex() == 1;
        s.site = m_siteEdit->text().trimmed();
        s.apiKey = m_apiKeyEdit->text().trimmed();
        s.clearance = m_clearanceEdit->text().trimmed();
        s.userAgent = m_userAgentEdit->text().trimmed();
    }
    s.currency = m_liveCurrencyCombo->currentText().trimmed().toLower();
    s.remember = m_rememberCheck->isChecked();
    s.extensionPort = m_extensionPort;
    return s;
}

void OptionsDialog::setLiveSettings(const LiveSettings &s)
{
    m_base = s;
    m_shuffle = s.isShuffle();
    m_goated = s.isGoated();
    m_wolfbet = s.isWolfbet();
    applyCasinoUi();

    m_liveEnable->setChecked(s.enabled);
    if (m_wolfbet) {
        m_authCombo->setCurrentIndex(0); // Direct mode
        m_apiKeyEdit->setText(s.wolfbetToken);
    } else if (m_goated) {
        m_authCombo->setCurrentIndex(0); // Direct mode
        m_apiKeyEdit->setText(s.goatedToken);
    } else if (m_shuffle) {
        m_authCombo->setCurrentIndex(0); // Direct mode
        m_siteEdit->setText(s.shuffleSite);
        m_apiKeyEdit->setText(s.shuffleToken);
        m_correlationEdit->setText(s.correlationUserId);
        m_browserIdEdit->setText(s.browserId);
    } else {
        m_authCombo->setCurrentIndex(s.extensionMode ? 1 : 0);
        m_siteEdit->setText(s.site);
        m_apiKeyEdit->setText(s.apiKey);
        m_clearanceEdit->setText(s.clearance);
        m_userAgentEdit->setText(s.userAgent);
    }
    m_liveCurrencyCombo->setEditText(s.currency);
    m_rememberCheck->setChecked(s.remember);
    m_extensionPort = s.extensionPort;
    updateAuthFields();
}

void OptionsDialog::setLoginStatus(const QString &text, bool ok)
{
    m_loginStatus->setText(text);
    m_loginStatus->setStyleSheet(ok ? QStringLiteral("color:#2e9e46;")
                                    : QStringLiteral("color:#c9372a;"));
    m_loginBtn->setEnabled(true);
}

void OptionsDialog::setLiveCurrencies(const QStringList &codes, const QString &selected)
{
    m_liveCurrencyCombo->clear();
    m_liveCurrencyCombo->addItems(codes);
    const int idx = m_liveCurrencyCombo->findText(selected, Qt::MatchFixedString);
    if (idx >= 0)
        m_liveCurrencyCombo->setCurrentIndex(idx);
}

void OptionsDialog::setExtensionStatus(bool listening, bool connected)
{
    if (!listening)
        m_extStatus->setText(tr("server off (select \"Browser extension\" and log in)"));
    else if (connected)
        m_extStatus->setText(tr("connected"));
    else
        m_extStatus->setText(tr("waiting for the extension on ws://127.0.0.1:%1").arg(m_extensionPort));
}

void OptionsDialog::onGrabCookie()
{
    const QString site = m_siteEdit->text().trimmed();
    if (site.isEmpty()) {
        setLoginStatus(tr("Enter the site / mirror domain first."), false);
        return;
    }

    const QString currentUA = m_userAgentEdit->text().trimmed();
    
    CookieGrabberDialog dlg(site, currentUA, this);
    if (dlg.exec() == QDialog::Accepted) {
        if (!dlg.clearance().isEmpty())
            m_clearanceEdit->setText(dlg.clearance());
        if (!dlg.userAgent().isEmpty())
            m_userAgentEdit->setText(dlg.userAgent());
            
        setLoginStatus(tr("cf_clearance captured — press Log in to verify."), true);
    }
}
void OptionsDialog::onShuffleLogin()
{
    ShuffleLoginDialog dlg(m_siteEdit->text().trimmed(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    m_correlationEdit->setText(dlg.correlationUserId());
    m_browserIdEdit->setText(dlg.browserId());
    setLoginStatus(tr("Logged in to Shuffle — press Log in to verify."), true);
}

void OptionsDialog::onGoatedLogin()
{
    GoatedLoginDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    setLoginStatus(tr("Logged in to Goated — press Log in to verify."), true);
}

void OptionsDialog::onWolfbetLogin()
{
    WolfbetLoginDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    setLoginStatus(tr("Logged in to Wolfbet — press Log in to verify."), true);
}
