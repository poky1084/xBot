#include "ui/CookieGrabberDialog.h"
#include "ui/CursorRestore.h"
#include "ui/GoatedLoginDialog.h"
#include "ui/OptionsDialog.h"
#include "ui/ShuffleLoginDialog.h"
#include "ui/DuelLoginDialog.h"
#include "ui/WolfbetLoginDialog.h"
#include "bot/CasinoCurrencies.h"
#include "bot/StakeTransport.h" // ExtensionTransport::bareHost / stakeHosts

#include <QAction>
#include <QCheckBox>
#include <QEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
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
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#endif


namespace
{
// Eye icon drawn in code (no resource needed), in the line edit's text colour so it fits the
// light and dark theme. `slashed` = the "hide" variant.
QIcon eyeIcon(const QColor &color, bool slashed)
{
    QIcon icon;
    for (const int size : {16, 24, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 16.0, size / 16.0);
        p.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath outline;
        outline.moveTo(1.5, 8.0);
        outline.quadTo(8.0, 1.8, 14.5, 8.0);
        outline.quadTo(8.0, 14.2, 1.5, 8.0);
        p.drawPath(outline);
        p.setBrush(color);
        p.drawEllipse(QPointF(8.0, 8.0), 2.2, 2.2);
        if (slashed) {
            p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(2.5, 13.5), QPointF(13.5, 2.5));
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// Keeps the eye's colour in step with the input: the theme (style sheet / palette) is applied
// after the dialog is built, so an icon drawn in the constructor would use the wrong colour
// (e.g. black on the dark theme) and look invisible until clicked.
class EyeUpdater final : public QObject
{
public:
    EyeUpdater(QLineEdit *edit, QAction *act)
        : QObject(edit), m_edit(edit), m_act(act)
    {
        edit->installEventFilter(this);
    }

    void update()
    {
        const bool shown = m_edit->echoMode() == QLineEdit::Normal;
        m_act->setIcon(eyeIcon(m_edit->palette().color(QPalette::Text), shown));
        m_act->setToolTip(shown ? OptionsDialog::tr("Hide") : OptionsDialog::tr("Show"));
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == m_edit
            && (event->type() == QEvent::Show || event->type() == QEvent::PaletteChange
                || event->type() == QEvent::StyleChange))
            update();
        return QObject::eventFilter(obj, event);
    }

private:
    QLineEdit *m_edit;
    QAction *m_act;
};

// Adds a show / hide eye at the right end of a password-style input.
void addRevealButton(QLineEdit *edit)
{
    QAction *act = edit->addAction(QIcon(), QLineEdit::TrailingPosition);
    act->setCheckable(true);
    auto *updater = new EyeUpdater(edit, act);
    updater->update();
    QObject::connect(act, &QAction::toggled, edit, [edit, updater](bool shown) {
        edit->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        updater->update();
    });
}
} // namespace

OptionsDialog::OptionsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Options — xBot"));
    setMinimumWidth(300);
    setSizeGripEnabled(true); // resizable by dragging the edges / corner

    auto *root = new QVBoxLayout(this);
    // Two columns: Live betting + Data on the left, Appearance + Game on the right.
    // The columns live in a scroll area, so the window can be made narrower than the controls
    // need (scrollbars appear) instead of being stopped at the layout's minimum width.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *columns = new QHBoxLayout(content);
    columns->setContentsMargins(0, 0, 0, 0);
    auto *leftCol = new QVBoxLayout;
    auto *rightCol = new QVBoxLayout;
    columns->addLayout(rightCol, 1); // Live betting + Data on the left
    columns->addLayout(leftCol, 1);  // Appearance + Game on the right
    root->addWidget(scroll, 1);

    auto *appearanceBox = new QGroupBox(tr("Appearance"), this);
    auto *appearanceForm = new QFormLayout(appearanceBox);
    appearanceForm->setContentsMargins(8, 10, 8, 8);

    m_themeCombo = new QComboBox;
    m_themeCombo->addItem(tr("Light"));
    m_themeCombo->addItem(tr("Dark"));
    appearanceForm->addRow(tr("Theme:"), m_themeCombo);

    // The "Currency label" row was removed from the window; the (hidden) combo keeps the
    // simulation label value flowing through currencyTag() / setCurrencyTag() unchanged.
    m_currencyCombo = new QComboBox(this);
    m_currencyCombo->hide();
    m_currencyCombo->setEditable(true);
    m_currencyCombo->addItems({QStringLiteral("USDT"), QStringLiteral("BTC"),
                               QStringLiteral("ETH"), QStringLiteral("LTC"),
                               QStringLiteral("DOGE"), QStringLiteral("TRX")});

    leftCol->addWidget(appearanceBox);

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
                                  "Applies to auto-bet and scripts."));
    gameForm->addRow(tr("Delays between bet:"), m_betDelaySpin);

    m_retryNoBalanceCheck = new QCheckBox(tr("Retry bet when bet higher than balance"));
    m_retryNoBalanceCheck->setToolTip(
        tr("Live betting: when the next bet is higher than the current balance, keep the "
           "auto-bet running and check again every 5 seconds (e.g. until you deposit) instead of "
           "stopping."));
    gameForm->addRow(QString(), m_retryNoBalanceCheck);

    auto *note = new QLabel(
        tr(""),
        gameBox);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#737373;"));
    gameForm->addRow({}, note);

    leftCol->addWidget(gameBox);
    leftCol->addStretch();

    // ------------------------------------------------------------- live betting
    auto *liveBox = new QGroupBox(tr("Live betting"), this);
    m_liveBox = liveBox;
    auto *liveForm = new QFormLayout(liveBox);
    m_liveForm = liveForm;
    liveForm->setContentsMargins(8, 10, 8, 8);

    m_liveEnable = new QCheckBox(tr("Enable live betting"));
    liveForm->addRow(m_liveEnable);

    m_casinoCombo = new QComboBox;
    m_casinoCombo->addItem(tr("Stake.com"), int(LiveSettings::Casino::Stake));
    m_casinoCombo->addItem(tr("Shuffle.com"), int(LiveSettings::Casino::Shuffle));
    m_casinoCombo->addItem(tr("Goated.com"), int(LiveSettings::Casino::Goated));
    m_casinoCombo->addItem(tr("Wolfbet.com"), int(LiveSettings::Casino::Wolfbet));
    m_casinoCombo->addItem(tr("Duel.com"), int(LiveSettings::Casino::Duel));
    m_casinoCombo->setToolTip(tr("Casino to bet on (same as the Casino menu)"));
    liveForm->addRow(tr("Casino:"), m_casinoCombo);

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
    m_correlationEdit->setPlaceholderText(tr("CORRELATION_USER_ID from localstorage"));
    liveForm->addRow(tr("USER_ID:"), m_correlationEdit);

    m_browserIdEdit = new QLineEdit;
	m_browserIdEdit->setEchoMode(QLineEdit::Password);
    m_browserIdEdit->setPlaceholderText(tr("BROWSER_ID from localstorage"));
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

    // Duel only: UUID and cf_clearance (the Token is the "duel" cookie).
    m_duelUuidEdit = new QLineEdit;
    m_duelUuidEdit->setEchoMode(QLineEdit::Password);
    m_duelUuidEdit->setPlaceholderText(tr("localStorage.getItem('security:uuid')"));
    liveForm->addRow(tr("UUID:"), m_duelUuidEdit);

    m_duelClearanceEdit = new QLineEdit;
    m_duelClearanceEdit->setEchoMode(QLineEdit::Password);
    m_duelClearanceEdit->setPlaceholderText(tr("cf_clearance cookie value"));
    liveForm->addRow(tr("cf_clearance:"), m_duelClearanceEdit);

    // Duel only: opens the site, waits for the login, then fills Token / UUID / cf_clearance.
    m_duelLoginBtn = new QPushButton(tr("Open Duel.com && Log in - to autofill data"));
    m_duelLoginBtn->setToolTip(
        tr("Opens the site in a browser window. After you log in, the Token (\"duel\" cookie), "
           "UUID and cf_clearance are filled in automatically."));
    m_duelLoginBtn->setEnabled(DuelLoginDialog::isAvailable());
    liveForm->addRow(QString(), m_duelLoginBtn);
    connect(m_duelLoginBtn, &QPushButton::clicked, this, &OptionsDialog::onDuelLogin);

    m_clearanceEdit = new QLineEdit;
    m_clearanceEdit->setEchoMode(QLineEdit::Password);
    m_clearanceEdit->setPlaceholderText(tr("cf_clearance cookie value"));
    // Show / hide eye on every input with hidden text.
    for (QLineEdit *edit : {m_apiKeyEdit, m_correlationEdit, m_browserIdEdit, m_duelUuidEdit,
                            m_duelClearanceEdit, m_clearanceEdit})
        addRevealButton(edit);
    liveForm->addRow(tr("cf_clearance:"), m_clearanceEdit); // Stake only (Duel has its own)

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

    // Dropdown of every currency the casino supports (see bot/CasinoCurrencies.h); it can
    // still be typed in. Each casino keeps its own value (switching the Casino selector
    // shows that casino's). The balance is fetched as soon as a currency is picked / typed,
    // also when the account does not hold it (balance 0).
    m_liveCurrencyCombo = new QComboBox;
    m_liveCurrencyCombo->setEditable(true);
    m_liveCurrencyCombo->setInsertPolicy(QComboBox::NoInsert);
    m_liveCurrencyCombo->setMaxVisibleItems(20);
    m_liveCurrencyCombo->lineEdit()->setPlaceholderText(tr("pick or type the currency"));
    m_liveCurrencyCombo->setToolTip(tr("Currency to bet with, per casino. A currency the "
                                       "account does not hold has a balance of 0."));
    m_liveCurrencyLabel = new QLabel(tr("Bet currency:"));
    liveForm->addRow(m_liveCurrencyLabel, m_liveCurrencyCombo);
    connect(m_liveCurrencyCombo, QOverload<int>::of(&QComboBox::activated), this,
            &OptionsDialog::onCurrencyEdited);
    connect(m_liveCurrencyCombo->lineEdit(), &QLineEdit::editingFinished, this,
            &OptionsDialog::onCurrencyEdited);

    auto *loginRow = new QWidget;
    auto *loginLayout = new QHBoxLayout(loginRow);
    loginLayout->setContentsMargins(0, 0, 0, 0);
    m_loginBtn = new QPushButton(tr("Log in"));
    m_loginStatus = new QLabel;
    m_loginStatus->setWordWrap(true);
    loginLayout->addWidget(m_loginBtn);
    loginLayout->addWidget(m_loginStatus, 1);
    liveForm->addRow(loginRow);


    // Auto fetch: once every login input is filled in, the currencies are loaded without
    // pressing "Log in". Typing / pasting waits a moment, leaving the field fetches at once.
    m_autoFetchTimer = new QTimer(this);
    m_autoFetchTimer->setSingleShot(true);
    m_autoFetchTimer->setInterval(800);
    connect(m_autoFetchTimer, &QTimer::timeout, this, &OptionsDialog::maybeAutoFetch);

    connect(m_authCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { updateAuthFields(); scheduleAutoFetch(); });
    connect(m_casinoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &OptionsDialog::onCasinoChanged);
    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        const QString missing = missingLoginData();
        if (!missing.isEmpty()) {
            setLoginStatus(tr("Not logged in — %1").arg(missing), false);
            return;
        }
        setLoginStatus(tr("Logging in…"), true);
        m_loginBusy = true;
        m_fetchedCurrency = liveSettings().currency;
        const LiveSettings cur = liveSettings();
        m_autoFetchedKey[int(cur.casino)] = autoFetchKey(); // no extra auto fetch for these inputs
        emit loginRequested();
    });

    // A login result belongs to the inputs it was made with: editing them clears it.
    for (QLineEdit *edit : {m_siteEdit, m_apiKeyEdit, m_clearanceEdit, m_correlationEdit,
                            m_browserIdEdit, m_duelUuidEdit, m_duelClearanceEdit}) {
        if (!edit)
            continue;
        connect(edit, &QLineEdit::textEdited, m_loginStatus, &QLabel::clear);
        connect(edit, &QLineEdit::textEdited, this, &OptionsDialog::scheduleAutoFetch);
        connect(edit, &QLineEdit::editingFinished, this, &OptionsDialog::maybeAutoFetch);
    }
    // The site / mirror is saved on every keystroke (see siteEdited()).
    connect(m_siteEdit, &QLineEdit::textEdited, this, &OptionsDialog::siteEdited);
    // Wolfbet's currency list needs the token: fetch it once one is entered.
    connect(m_apiKeyEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_wolfbet)
            updateCurrencyList();
    });

    rightCol->addWidget(liveBox);
    applyCasinoUi();
    updateAuthFields();
    setExtensionStatus(false, QStringList());

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
    rightCol->addWidget(dataBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // Opens wide enough for both columns; the width can then be changed freely.
    resize(qMax(900, content->sizeHint().width() + 40), content->sizeHint().height() + 150); // 60 px taller than before
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

bool OptionsDialog::retryNoBalance() const
{
    return m_retryNoBalanceCheck->isChecked();
}

void OptionsDialog::setRetryNoBalance(bool on)
{
    m_retryNoBalanceCheck->setChecked(on);
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
    const bool duel = m_duel;
    const bool tokenOnly = goated || wolfbet;        // the token is the only input
    const bool noSite = tokenOnly;                   // no "Site / mirror" input
    const bool direct = shuffle || noSite || duel; // all but Stake use "Direct mode"

    m_liveBox->setTitle(duel ? tr("Live betting — Duel")
                        : wolfbet ? tr("Live betting — Wolfbet")
                                : goated ? tr("Live betting — Goated")
                                         : shuffle ? tr("Live betting — Shuffle") : tr("Live betting"));

    // Login method: every casino can send its requests through the browser extension (its own
    // tab in the browser). Index 1 is always "Browser extension".
    {
        const QSignalBlocker blocker(m_authCombo);
        m_authCombo->clear();
        m_authCombo->addItem(direct ? tr("Direct mode") : tr("Cookie (cf_clearance)"));
        m_authCombo->addItem(tr("Browser extension"));
        m_authCombo->setCurrentIndex(0);
    }

    // "API key" is the "Token" on Shuffle, Goated and Wolfbet.
    if (auto *label = qobject_cast<QLabel *>(m_liveForm->labelForField(m_apiKeyEdit)))
        label->setText(direct ? tr("Token:") : tr("API key:"));
    m_apiKeyEdit->setPlaceholderText(duel ? tr("\"duel\" cookie value")
                                     : wolfbet ? tr("\"token\" cookie (value / JWT)")
                                             : goated ? tr("g3_token (JWT)")
                                                      : shuffle ? tr("authorization token (JWT)")
                                                                : tr("x-access-token (API key)"));
    m_siteEdit->setPlaceholderText(duel      ? QStringLiteral("duel.com")
                                   : shuffle ? QStringLiteral("shuffle.com")
                                             : QStringLiteral("stake.com"));

    setFormRowVisible(m_siteEdit, !noSite);        // Goated / Wolfbet: fixed site
    setFormRowVisible(m_clearanceEdit, !direct);   // Stake's cf_clearance
    setFormRowVisible(m_correlationEdit, shuffle);
    setFormRowVisible(m_browserIdEdit, shuffle);
    setFormRowVisible(m_shuffleLoginBtn, shuffle);
    setFormRowVisible(m_goatedLoginBtn, goated);
    setFormRowVisible(m_wolfbetLoginBtn, wolfbet);
    setFormRowVisible(m_duelUuidEdit, duel);
    setFormRowVisible(m_duelClearanceEdit, duel);
    setFormRowVisible(m_duelLoginBtn, duel);
    setFormRowVisible(m_extStatus, true); // every casino can use the extension
}

void OptionsDialog::updateAuthFields()
{
    const bool ext = m_authCombo->currentIndex() == 1; // "Browser extension" (every casino)
    m_clearanceEdit->setEnabled(true); // typed in, or read from the Stake tab in extension mode
    m_userAgentEdit->setEnabled(!ext);
    m_extStatus->setEnabled(ext);
    if (m_grabCookieBtn)
        m_grabCookieBtn->setEnabled(!ext && !m_shuffle && !m_goated && !m_wolfbet && !m_duel
                                    && CookieGrabberDialog::isAvailable());
    updateExtensionStatusText();
}

LiveSettings OptionsDialog::liveSettings() const
{
    // Start from what we were given so the other casino's values are kept.
    LiveSettings s = m_base;
    s.enabled = m_liveEnable->isChecked();
    s.casino = m_duel ? LiveSettings::Casino::Duel
               : m_wolfbet ? LiveSettings::Casino::Wolfbet
               : m_goated ? LiveSettings::Casino::Goated
                          : m_shuffle ? LiveSettings::Casino::Shuffle : LiveSettings::Casino::Stake;
    if (m_duel) {
        s.duelSite = LiveSettings::hostOf(m_siteEdit->text(), QStringLiteral("duel.com"));
        s.duelToken = m_apiKeyEdit->text().trimmed();
        s.duelUuid = m_duelUuidEdit->text().trimmed();
        s.duelClearance = m_duelClearanceEdit->text().trimmed();
    } else if (m_wolfbet) {
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
    s.setUsesExtension(s.casino, m_authCombo->currentIndex() == 1);
    s.currency = m_liveCurrencyCombo->currentText().trimmed().toLower();
    s.extensionPort = m_extensionPort;
    return s;
}

void OptionsDialog::setLiveSettings(const LiveSettings &s)
{
    m_base = s;
    m_shuffle = s.isShuffle();
    m_goated = s.isGoated();
    m_wolfbet = s.isWolfbet();
    m_duel = s.isDuel();
    {
        const QSignalBlocker blocker(m_casinoCombo);
        m_casinoCombo->setCurrentIndex(qMax(0, m_casinoCombo->findData(int(s.casino))));
    }
    m_casinoCurrency[int(s.casino)] = s.currency;
    applyCasinoUi();

    m_liveEnable->setChecked(s.enabled);
    if (m_duel) {
        m_authCombo->setCurrentIndex(s.usesExtension(s.casino) ? 1 : 0); // Direct mode / extension
        m_siteEdit->setText(s.duelSite);
        m_apiKeyEdit->setText(s.duelToken);
        m_duelUuidEdit->setText(s.duelUuid);
        m_duelClearanceEdit->setText(s.duelClearance);
    } else if (m_wolfbet) {
        m_authCombo->setCurrentIndex(s.usesExtension(s.casino) ? 1 : 0); // Direct mode / extension
        m_apiKeyEdit->setText(s.wolfbetToken);
    } else if (m_goated) {
        m_authCombo->setCurrentIndex(s.usesExtension(s.casino) ? 1 : 0); // Direct mode / extension
        m_apiKeyEdit->setText(s.goatedToken);
    } else if (m_shuffle) {
        m_authCombo->setCurrentIndex(s.usesExtension(s.casino) ? 1 : 0); // Direct mode / extension
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
    {
        const QSignalBlocker blocker(m_liveCurrencyCombo);
        m_liveCurrencyCombo->setEditText(s.currency);
    }
    m_fetchedCurrency = s.currency; // a stored value is not "newly typed"
    updateCurrencyList();
    m_extensionPort = s.extensionPort;
    updateAuthFields();
    // Deferred: the dialog's owner connects to loginRequested() after setLiveSettings().
    QTimer::singleShot(0, this, &OptionsDialog::maybeAutoFetch);
}

void OptionsDialog::setCasinoCurrencies(const QStringList &codes)
{
    for (int i = 0; i < LiveSettings::kCasinoCount; ++i)
        m_casinoCurrency[i] = i < codes.size() ? codes.at(i) : QString();
}

QStringList OptionsDialog::casinoCurrencies() const
{
    QStringList out;
    for (int i = 0; i < LiveSettings::kCasinoCount; ++i)
        out << m_casinoCurrency[i];
    const LiveSettings cur = liveSettings();
    out[int(cur.casino)] = cur.currency;
    return out;
}

void OptionsDialog::onCasinoChanged(int index)
{
    // Keep what was typed for the casino being left (applied with OK, dropped with Cancel).
    LiveSettings s = liveSettings();
    m_casinoCurrency[int(s.casino)] = s.currency;

    s.casino = static_cast<LiveSettings::Casino>(m_casinoCombo->itemData(index).toInt());
    s.currency = m_casinoCurrency[int(s.casino)];

    // The login result and the account's currencies belong to the previous casino.
    m_accountCurrencies.clear();
    m_loginStatus->clear();
    setLiveSettings(s);
}

void OptionsDialog::setLoginStatus(const QString &text, bool ok)
{
    m_loginStatus->setText(text);
    m_loginStatus->setStyleSheet(ok ? QStringLiteral("color:#2e9e46;")
                                    : QStringLiteral("color:#c9372a;"));
    m_loginBusy = false;
}

void OptionsDialog::setLiveCurrencies(const QStringList &codes, const QString &selected)
{
    // Never changes the chosen currency; the account's currencies are added to the list
    // (for casinos without a known list they are the list) and named in the tooltip.
    Q_UNUSED(selected);
    // Wolfbet's balance answer lists every currency it supports (also those at 0): it is
    // the same list the dropdown fetches, so keep it.
    const auto casino = static_cast<LiveSettings::Casino>(m_casinoCombo->currentData().toInt());
    if (casino == LiveSettings::Casino::Wolfbet && codes.size() > 1) {
        QStringList all;
        for (const QString &c : codes)
            all << c.trimmed().toLower();
        all.removeAll(QString());
        std::sort(all.begin(), all.end());
        CasinoCurrencies::store(casino, all);
    }
    // Duel: the account's currencies (user.balances) are merged into the wallet list.
    if (casino == LiveSettings::Casino::Duel && !codes.isEmpty()) {
        QStringList all = CasinoCurrencies::cached(casino);
        for (const QString &c : codes) {
            const QString code = c.trimmed().toLower();
            if (!code.isEmpty() && !all.contains(code))
                all << code;
        }
        CasinoCurrencies::store(casino, all);
    }
    // Stake: its balance answer is the only currency source (the site is reached through
    // the hidden browser / extension). Merge it into the cached list so it never shrinks.
    if (casino == LiveSettings::Casino::Stake && !codes.isEmpty()) {
        QStringList all = CasinoCurrencies::cached(casino);
        for (const QString &c : codes) {
            const QString code = c.trimmed().toLower();
            if (!code.isEmpty() && !all.contains(code))
                all << code;
        }
        std::sort(all.begin(), all.end());
        CasinoCurrencies::store(casino, all);
    }
    m_accountCurrencies.clear();
    QStringList upper;
    for (const QString &c : codes) {
        m_accountCurrencies << c.trimmed().toLower();
        upper << c.toUpper();
    }
    const QString list = upper.isEmpty() ? tr("none") : upper.join(QStringLiteral(", "));
    m_liveCurrencyCombo->setToolTip(tr("Currency to bet with, per casino. The account holds: "
                                       "%1. Any other currency has a balance of 0.")
                                        .arg(list));
    updateCurrencyList();
}

void OptionsDialog::updateCurrencyList()
{
    const auto casino = static_cast<LiveSettings::Casino>(
        m_casinoCombo->currentData().toInt());

    QStringList items = CasinoCurrencies::cached(casino);
    for (const QString &c : std::as_const(m_accountCurrencies)) {
        if (!c.isEmpty() && !items.contains(c))
            items << c;
    }
    const QString chosen = m_liveCurrencyCombo->currentText();
    {
        const QSignalBlocker blocker(m_liveCurrencyCombo);
        m_liveCurrencyCombo->clear();
        m_liveCurrencyCombo->addItems(items);
        const int idx = m_liveCurrencyCombo->findText(chosen.trimmed(), Qt::MatchFixedString);
        if (idx >= 0)
            m_liveCurrencyCombo->setCurrentIndex(idx);
        else
            m_liveCurrencyCombo->setEditText(chosen);
    }

    // Fetch the casino's full list once per dialog; the cached one is shown meanwhile.
    // Wolfbet's list needs the token: it is fetched once one is filled in.
    if (CasinoCurrencies::hasSource(casino) && !m_currencyListFetched[int(casino)]) {
        const QString token = m_apiKeyEdit ? m_apiKeyEdit->text().trimmed() : QString();
        m_currencyListFetched[int(casino)] = CasinoCurrencies::fetch(
            casino, this,
            [this, casino](const QStringList &) {
                if (int(casino) == m_casinoCombo->currentData().toInt())
                    updateCurrencyList();
            },
            token);
    }
}

void OptionsDialog::onCurrencyEdited()
{
    const QString cur = liveSettings().currency;
    if (cur == m_fetchedCurrency)
        return; // unchanged (editingFinished also fires on focus loss)
    m_fetchedCurrency = cur;
    if (cur.isEmpty()) {
        m_loginStatus->clear();
        return;
    }
    const QString missing = missingLoginData();
    if (!missing.isEmpty()) {
        setLoginStatus(tr("Not logged in — %1").arg(missing), false);
        return;
    }
    setLoginStatus(tr("Loading the %1 balance…").arg(cur.toUpper()), true);
    m_loginBusy = true;
    emit loginRequested(); // same as "Log in": applies the inputs and fetches the balances
}

QString OptionsDialog::missingLoginData() const
{
    const auto empty = [](const QLineEdit *e) { return !e || e->text().trimmed().isEmpty(); };
    // "Browser extension": every token / id / API key is read from the casino's tab.
    if (m_authCombo && m_authCombo->currentIndex() == 1)
        return QString();
    if (m_goated || m_wolfbet)
        return empty(m_apiKeyEdit) ? tr("the token is empty.") : QString();
    if (m_duel) {
        if (empty(m_apiKeyEdit))
            return tr("the token (\"duel\" cookie) is empty.");
        if (empty(m_duelUuidEdit))
            return tr("the UUID is empty.");
        return QString(); // cf_clearance is optional (only needed when Cloudflare challenges)
    }
    if (m_shuffle) {
        if (empty(m_apiKeyEdit))
            return tr("the token is empty.");
        if (empty(m_correlationEdit))
            return tr("CORRELATION_USER_ID is empty.");
        if (empty(m_browserIdEdit))
            return tr("BROWSER_ID is empty.");
        return QString();
    }
    if (empty(m_siteEdit))
        return tr("the site / mirror is empty.");
    if (empty(m_apiKeyEdit))
        return tr("the API key is empty.");
    return QString();
}

void OptionsDialog::setAutoFetchEnabled(bool on)
{
    m_autoFetchEnabled = on;
    if (!on && m_autoFetchTimer)
        m_autoFetchTimer->stop();
}

QString OptionsDialog::autoFetchKey() const
{
    const LiveSettings s = liveSettings();
    QStringList parts{s.casinoName(), s.activeUsesExtension() ? QStringLiteral("ext") : QStringLiteral("direct")};
    if (s.activeUsesExtension()) // the login comes from the tab: only the site matters
        return (parts + s.extensionHosts(s.casino)).join(QChar(0x1f));
    if (s.isDuel())
        parts << s.duelSite << s.duelToken << s.duelUuid << s.duelClearance;
    else if (s.isWolfbet())
        parts << s.wolfbetToken;
    else if (s.isGoated())
        parts << s.goatedToken;
    else if (s.isShuffle())
        parts << s.shuffleSite << s.shuffleToken << s.correlationUserId << s.browserId;
    else
        parts << (s.extensionMode ? QStringLiteral("ext") : QStringLiteral("cookie")) << s.site
              << s.apiKey << s.clearance << s.userAgent;
    return parts.join(QChar(0x1f));
}

void OptionsDialog::scheduleAutoFetch()
{
    if (m_autoFetchEnabled && m_autoFetchTimer)
        m_autoFetchTimer->start();
}

void OptionsDialog::maybeAutoFetch()
{
    if (m_autoFetchTimer)
        m_autoFetchTimer->stop();
    if (!m_autoFetchEnabled || m_shuffle) // Shuffle: the currency list is built in
        return;
    if (!missingLoginData().isEmpty())
        return; // not every input is filled in yet
    if (m_loginBusy)
        return; // a login / balance fetch is already running
    const LiveSettings s = liveSettings();
    const QString key = autoFetchKey();
    QString &done = m_autoFetchedKey[int(s.casino)];
    if (done == key)
        return; // already fetched with exactly these inputs
    done = key;

    setLoginStatus(tr("Loading the currencies…"), true);
    m_loginBusy = true;
    m_fetchedCurrency = s.currency;
    emit loginRequested(); // same as "Log in": applies the inputs and fetches the balances
}

void OptionsDialog::setGrabbedCredentials(const LiveSettings &grabbed)
{
    m_base.copyCredentials(grabbed.casino, grabbed); // also kept when switching casinos here
    const LiveSettings cur = liveSettings();
    if (cur.casino != grabbed.casino)
        return;
    switch (grabbed.casino) {
    case LiveSettings::Casino::Shuffle:
        m_apiKeyEdit->setText(grabbed.shuffleToken);
        m_correlationEdit->setText(grabbed.correlationUserId);
        m_browserIdEdit->setText(grabbed.browserId);
        break;
    case LiveSettings::Casino::Goated:
        m_apiKeyEdit->setText(grabbed.goatedToken);
        break;
    case LiveSettings::Casino::Wolfbet:
        m_apiKeyEdit->setText(grabbed.wolfbetToken);
        break;
    case LiveSettings::Casino::Duel:
        if (!grabbed.duelToken.isEmpty())
            m_apiKeyEdit->setText(grabbed.duelToken);
        m_duelUuidEdit->setText(grabbed.duelUuid);
        if (!grabbed.duelClearance.isEmpty())
            m_duelClearanceEdit->setText(grabbed.duelClearance);
        break;
    default:
        m_apiKeyEdit->setText(grabbed.apiKey);
        if (!grabbed.clearance.isEmpty())
            m_clearanceEdit->setText(grabbed.clearance);
        break;
    }
}

void OptionsDialog::setExtensionStatus(bool listening, const QStringList &hosts)
{
    m_extListening = listening;
    m_extHosts = hosts;
    updateExtensionStatusText();
}

// Shows whether the shown casino's tab is connected; when it just connected (and its login
// method is the extension), the currencies can now be read.
void OptionsDialog::updateExtensionStatusText()
{
    if (!m_extStatus || !m_authCombo)
        return;
    const LiveSettings s = liveSettings();
    const QStringList want = s.extensionHosts(s.casino);
    bool connected = false;
    for (const QString &h : want) {
        const QString bare = ExtensionTransport::bareHost(h);
        for (const QString &have : m_extHosts)
            connected = connected || ExtensionTransport::bareHost(have) == bare;
    }
    // Stake: a tab on any Stake domain works too.
    if (!connected && s.casino == LiveSettings::Casino::Stake) {
        for (const QString &have : m_extHosts)
            connected = connected || ExtensionTransport::stakeHosts().contains(ExtensionTransport::bareHost(have));
    }
    connected = connected && m_extListening;

    const bool ext = m_authCombo->currentIndex() == 1;
    const bool nowConnected = connected && !m_extConnected;
    m_extConnected = connected;
    if (nowConnected && ext) {
        m_autoFetchedKey[int(s.casino)].clear();
        QTimer::singleShot(0, this, &OptionsDialog::maybeAutoFetch);
    }

    const QString site = want.isEmpty() ? s.activeSite() : want.first();
    if (!m_extListening)
        m_extStatus->setText(ext ? tr("server off (press Log in to start it)")
                                 : tr("server off (select \"Browser extension\" and log in)"));
    else if (connected)
        m_extStatus->setText(tr("connected (%1 tab)").arg(site));
    else
        m_extStatus->setText(tr("waiting for a %1 tab with the extension (ws://127.0.0.1:%2)")
                                 .arg(site)
                                 .arg(m_extensionPort));
}

void OptionsDialog::onGrabCookie()
{
    const QString site = m_siteEdit->text().trimmed();
    if (site.isEmpty()) {
        setLoginStatus(tr("Enter the site / mirror domain first."), false);
        return;
    }

    const QString currentUA = m_userAgentEdit->text().trimmed();
    
    const CursorRestorer restoreCursor; // WebView2 bug: the pointer may stay hidden after `dlg`
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
    const CursorRestorer restoreCursor; // WebView2 bug: the pointer may stay hidden after `dlg`
    ShuffleLoginDialog dlg(m_siteEdit->text().trimmed(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    m_correlationEdit->setText(dlg.correlationUserId());
    m_browserIdEdit->setText(dlg.browserId());
    setLoginStatus(tr("Shuffle token captured — press Log in to verify."), true);
    // Shuffle's currency list is built in: no auto fetch.
}

void OptionsDialog::onGoatedLogin()
{
    const CursorRestorer restoreCursor; // WebView2 bug: the pointer may stay hidden after `dlg`
    GoatedLoginDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    setLoginStatus(tr("Goated token captured."), true);
    maybeAutoFetch(); // loads the account's currencies
}

void OptionsDialog::onWolfbetLogin()
{
    const CursorRestorer restoreCursor; // WebView2 bug: the pointer may stay hidden after `dlg`
    WolfbetLoginDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    setLoginStatus(tr("Wolfbet token captured."), true);
    updateCurrencyList(); // the currency list needs the token
    maybeAutoFetch();     // loads the account's currencies
}

void OptionsDialog::onDuelLogin()
{
    const CursorRestorer restoreCursor; // WebView2 bug: the pointer may stay hidden after `dlg`
    DuelLoginDialog dlg(LiveSettings::hostOf(m_siteEdit->text(), QStringLiteral("duel.com")), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_apiKeyEdit->setText(dlg.token());
    m_duelUuidEdit->setText(dlg.uuid());
    m_duelClearanceEdit->setText(dlg.clearance());
    // cf_clearance only works with the User-Agent of the browser that got it: keep it with
    // the other settings (not shown, used by the Duel transport).
    if (!dlg.userAgent().isEmpty())
        m_base.duelUserAgent = dlg.userAgent();
    setLoginStatus(dlg.clearance().isEmpty()
                       ? tr("Duel token and UUID captured (no cf_clearance cookie was set).")
                       : tr("Duel token, UUID and cf_clearance captured."),
                   true);
    // The login check reads the balances (through the hidden Duel browser); the account's
    // currencies are merged into the list from that answer.
    m_autoFetchedKey[int(LiveSettings::Casino::Duel)].clear();
    maybeAutoFetch();
}
