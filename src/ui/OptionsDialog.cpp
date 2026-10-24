#include "ui/CookieGrabberDialog.h"
#include "ui/OptionsDialog.h"

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
    setWindowTitle(tr("Options — QDiceBot"));
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

    auto *note = new QLabel(
        tr(""),
        gameBox);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#737373;"));
    gameForm->addRow({}, note);

    root->addWidget(gameBox);

    // ------------------------------------------------------------- live betting
    auto *liveBox = new QGroupBox(tr("Live betting"), this);
    auto *liveForm = new QFormLayout(liveBox);
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

void OptionsDialog::setStartBalance(double value)
{
    m_startBalanceSpin->setValue(value);
}

// ------------------------------------------------------------------ live

void OptionsDialog::updateAuthFields()
{
    const bool ext = m_authCombo->currentIndex() == 1;
    m_clearanceEdit->setEnabled(!ext);
    m_userAgentEdit->setEnabled(!ext);
    m_extStatus->setEnabled(ext);
    if (m_grabCookieBtn)
        m_grabCookieBtn->setEnabled(!ext && CookieGrabberDialog::isAvailable());
}

LiveSettings OptionsDialog::liveSettings() const
{
    LiveSettings s;
    s.enabled = m_liveEnable->isChecked();
    s.extensionMode = m_authCombo->currentIndex() == 1;
    s.site = m_siteEdit->text().trimmed();
    s.apiKey = m_apiKeyEdit->text().trimmed();
    s.clearance = m_clearanceEdit->text().trimmed();
    s.userAgent = m_userAgentEdit->text().trimmed();
    s.currency = m_liveCurrencyCombo->currentText().trimmed().toLower();
    s.remember = m_rememberCheck->isChecked();
    s.extensionPort = m_extensionPort;
    return s;
}

void OptionsDialog::setLiveSettings(const LiveSettings &s)
{
    m_liveEnable->setChecked(s.enabled);
    m_authCombo->setCurrentIndex(s.extensionMode ? 1 : 0);
    m_siteEdit->setText(s.site);
    m_apiKeyEdit->setText(s.apiKey);
    m_clearanceEdit->setText(s.clearance);
    m_userAgentEdit->setText(s.userAgent);
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