#include "ui/FairPanel.h"

#include "bot/StakeFair.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include "ui/WindowSettings.h"
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

FairPanel::FairPanel(SimulatedProvider *provider, const QString &settingsGroup, QWidget *parent)
    : QWidget(parent)
    , m_provider(provider)
    , m_settingsGroup(settingsGroup)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(24);

    // ---- Left: active seeds ---------------------------------------------
    auto *left = new QVBoxLayout;
    left->setSpacing(6);
    left->addWidget(new QLabel(tr("<b>Active seeds</b>")));

    m_serverHashEdit = new QLineEdit;
    m_serverHashEdit->setReadOnly(true);
    m_serverHashEdit->setMinimumWidth(280);
    m_serverHashEdit->setFont(QFont(QStringLiteral("Consolas")));

    m_clientSeedEdit = new QLineEdit;
    m_clientSeedEdit->setFont(QFont(QStringLiteral("Consolas")));

    m_nonceLabel = new QLabel;
    m_nonceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);
    form->addRow(tr("Server seed (hashed):"), m_serverHashEdit);
    form->addRow(tr("Client seed:"), m_clientSeedEdit);
    form->addRow(tr("Nonce:"), m_nonceLabel);
    left->addLayout(form);

    auto *rotateBtn = new QPushButton(tr("Rotate server seed"));
    rotateBtn->setIcon(QIcon(QStringLiteral(":/icons/security-high.svg")));
    connect(rotateBtn, &QPushButton::clicked, this, &FairPanel::onRotate);
    left->addWidget(rotateBtn, 0, Qt::AlignLeft);

    m_prevList = new QListWidget;
    m_prevList->setMaximumHeight(90);
    m_prevList->setFont(QFont(QStringLiteral("Consolas")));
    connect(m_prevList, &QListWidget::itemClicked, this, &FairPanel::onPickRevealed);
    left->addWidget(m_prevList);
    left->addStretch();
    root->addLayout(left);

    // ---- Right: verify a roll -------------------------------------------
    auto *right = new QVBoxLayout;
    right->setSpacing(6);
    right->addWidget(new QLabel(tr("<b>Verify a roll</b>")));
    m_verifyGame = new QLabel;
    m_verifyGame->setStyleSheet(QStringLiteral("color:#8a8f94;"));
    m_verifyGame->setWordWrap(true);
    right->addWidget(m_verifyGame);

    m_verifyServerEdit = new QLineEdit;
    m_verifyServerEdit->setFont(QFont(QStringLiteral("Consolas")));
    m_verifyClientEdit = new QLineEdit;
    m_verifyClientEdit->setFont(QFont(QStringLiteral("Consolas")));
    m_verifyNonceSpin = new QSpinBox;
    m_verifyNonceSpin->setRange(1, std::numeric_limits<int>::max());

    auto *verifyForm = new QFormLayout;
    verifyForm->setContentsMargins(0, 0, 0, 0);
    verifyForm->setHorizontalSpacing(12);
    verifyForm->setVerticalSpacing(6);
    verifyForm->addRow(tr("Server seed:"), m_verifyServerEdit);
    verifyForm->addRow(tr("Client seed:"), m_verifyClientEdit);
    verifyForm->addRow(tr("Nonce:"), m_verifyNonceSpin);
    right->addLayout(verifyForm);

    // Checkbox that tells the SimulatedProvider to use these seeds.
    m_useSeedsCheck = new QCheckBox(tr("Use these seeds for simulation"));
    m_useSeedsCheck->setToolTip(tr("When checked, every simulated roll is generated "
                                   "from the server seed, client seed and nonce above "
                                   "instead of the active (random) seed pair."));
    right->addWidget(m_useSeedsCheck);

    auto *verifyBtn = new QPushButton(tr("Verify"));
    connect(verifyBtn, &QPushButton::clicked, this, &FairPanel::onVerify);
    right->addWidget(verifyBtn, 0, Qt::AlignLeft);

    m_verifyResult = new QLabel;
    m_verifyResult->setTextFormat(Qt::RichText);
    m_verifyResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_verifyResult->setFont(QFont(QStringLiteral("Consolas")));
    m_verifyResult->setWordWrap(true);
    right->addWidget(m_verifyResult);

    auto *hint = new QLabel(
        tr("Same algorithm as Stake: HMAC-SHA256(server, \"client:nonce:0\")."));
    hint->setStyleSheet(QStringLiteral("color:#737373;"));
    //right->addWidget(hint);
    right->addStretch();
    root->addLayout(right, 1);

    connect(m_clientSeedEdit, &QLineEdit::editingFinished, this, [this] {
        m_provider->setClientSeed(m_clientSeedEdit->text());
    });

    // Live preview: update the hash/roll as the user edits any of these fields.
    connect(m_verifyServerEdit, &QLineEdit::textChanged,
            this, &FairPanel::updateVerifyPreview);
    connect(m_verifyClientEdit, &QLineEdit::textChanged,
            this, &FairPanel::updateVerifyPreview);
    connect(m_verifyNonceSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &FairPanel::updateVerifyPreview);

    // Toggling the checkbox pushes the seeds into the provider.
    connect(m_useSeedsCheck, &QCheckBox::toggled, this, &FairPanel::onUseSeedsToggled);

    // Persist every input in the "Verify a roll" section. The save-on-toggled
    // connection is deliberately registered *after* onUseSeedsToggled so that
    // when onUseSeedsToggled auto-unchecks the box (seeds empty), saveSettings
    // stores the final state, not the stale "checked" one.
    connect(m_verifyServerEdit, &QLineEdit::editingFinished,
            this, &FairPanel::saveSettings);
    connect(m_verifyClientEdit, &QLineEdit::editingFinished,
            this, &FairPanel::saveSettings);
    connect(m_verifyNonceSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &FairPanel::saveSettings);
    connect(m_useSeedsCheck, &QCheckBox::toggled, this, &FairPanel::saveSettings);

    // Restore the saved "Verify a roll" inputs and checkbox state. If the box
    // was checked when the app last closed, re-apply those seeds to the
    // provider so a restart continues exactly where the user left off.
    loadSettings();

    refresh();
    updateVerifyPreview();
}

void FairPanel::setGame(Game game)
{
    m_game = game;
    updateVerifyPreview();
}

bool FairPanel::customSeeds(QString *server, QString *client, qint64 *nonce) const
{
    const QString s = m_verifyServerEdit->text().trimmed();
    const QString c = m_verifyClientEdit->text().trimmed();
    if (!m_useSeedsCheck->isChecked() || s.isEmpty() || c.isEmpty())
        return false;
    *server = s;
    *client = c;
    *nonce = m_verifyNonceSpin->value();
    return true;
}

bool FairPanel::latestRevealed(RevealedSeeds *out) const
{
    if (m_revealed.isEmpty())
        return false;
    *out = m_revealed.first();
    return true;
}

void FairPanel::setParamsSource(std::function<QJsonObject()> source)
{
    m_paramsSource = std::move(source);
    updateVerifyPreview();
}

void FairPanel::refresh()
{
    m_serverHashEdit->setText(m_provider->serverHash());
    m_clientSeedEdit->setText(m_provider->clientSeed());
    m_nonceLabel->setText(QString::number(m_provider->nonce()));
}

void FairPanel::onUseSeedsToggled(bool checked)
{
    if (checked) {
        // Can't use custom seeds without both of them.
        const QString server = m_verifyServerEdit->text().trimmed();
        const QString client = m_verifyClientEdit->text().trimmed();
        if (server.isEmpty() || client.isEmpty()) {
            QSignalBlocker blocker(m_useSeedsCheck);
            m_useSeedsCheck->setChecked(false);
            return;
        }
        m_provider->setServerSeed(server);
        m_provider->setClientSeed(client);
        m_provider->setNonce(m_verifyNonceSpin->value());
        refresh();
    } else {
        // Back to fresh random seeds.
        m_provider->randomizeSeeds();
        refresh();
    }
}

void FairPanel::onRotate()
{
    // Rotating produces a fresh random pair, so exit custom-seed mode first.
    if (m_useSeedsCheck && m_useSeedsCheck->isChecked()) {
        QSignalBlocker blocker(m_useSeedsCheck);
        //m_useSeedsCheck->setChecked(false);
    }

    const RevealedSeeds revealed = m_provider->rotateSeeds();
    m_revealed.prepend(revealed);

    const QString display =
        QStringLiteral("server: %1\nclient: %2 · %3 bets")
            .arg(revealed.serverSeed, revealed.clientSeed)
            .arg(revealed.nonces);
    m_prevList->insertItem(0, display);
    m_prevList->item(0)->setData(Qt::UserRole, revealed.nonces);

    refresh();
    //emit seedRotated(tr("Server seed rotated — previous seed revealed in Fairness tab"));
}

void FairPanel::onVerify()
{
    updateVerifyPreview();
}

void FairPanel::updateVerifyPreview()
{
    const QString server = m_verifyServerEdit->text().trimmed();
    const QString client = m_verifyClientEdit->text().trimmed();

    // If "use for simulation" is active, keep the provider in sync with the
    // fields as the user edits them.
    if (m_useSeedsCheck && m_useSeedsCheck->isChecked()
        && !server.isEmpty() && !client.isEmpty()) {
        m_provider->setServerSeed(server);
        m_provider->setClientSeed(client);
        m_provider->setNonce(m_verifyNonceSpin->value());
        refresh();
    }

    // Which game is verified, and which of its settings are used.
    const QJsonObject params = m_paramsSource ? m_paramsSource() : QJsonObject();
    QString gameLine = tr("Game: %1").arg(gameDisplayName(m_game));
    switch (m_game) {
    case Game::Keno: gameLine += tr(" (your numbers and risk from the Manual Bet tab)"); break;
    case Game::Plinko: gameLine += tr(" (rows and risk from the Manual Bet tab)"); break;
    case Game::Wheel: gameLine += tr(" (segments and risk from the Manual Bet tab)"); break;
    case Game::DragonTower: gameLine += tr(" (easy)"); break;
    case Game::Pump: case Game::Chicken: case Game::GoatRun:
        gameLine += tr(" (difficulty from the Manual Bet tab)"); break;
    case Game::Primedice: gameLine += tr(" (condition and targets from the Manual Bet tab)"); break;
    case Game::TomeOfLife: gameLine += tr(" (lines and bonus buy from the Manual Bet tab)"); break;
    case Game::ScarabSpin: gameLine += tr(" (lines from the Manual Bet tab)"); break;
    default: break;
    }
    m_verifyGame->setText(gameLine);

    // Nothing typed yet — show a short hint instead of an empty label.
    if (server.isEmpty()) {
        m_verifyResult->setText(
            QStringLiteral("<span style=\"color:#737373\">%1</span>")
                .arg(tr("Enter a server seed to see its SHA-256 hash.")));
        emit resultChanged();
        return;
    }

    // SHA-256 depends only on the server seed, so show it as soon as that
    // field has content — no need to wait for the client seed or Verify.
    const QString hash = SimulatedProvider::sha256Hex(server);

    if (client.isEmpty()) {
        m_verifyResult->setText(
            QStringLiteral("<span style=\"color:#737373\">SHA-256: %1</span>").arg(hash));
        emit resultChanged();
        return;
    }

    // Both seeds present: the game's outcome for this nonce, worked out like Stake does.
    StakeFair::Seeds seeds;
    seeds.server = server;
    seeds.client = client;
    seeds.nonce = m_verifyNonceSpin->value();
    m_verifyResult->setText(
        QStringLiteral("%1<br><span style=\"color:#737373\">SHA-256: %2</span>")
            .arg(StakeFair::verifyHtml(m_game, seeds, params), hash));
    emit resultChanged();
}

void FairPanel::onPickRevealed()
{
    const int row = m_prevList->currentRow();
    if (row < 0 || row >= m_revealed.size())
        return;
    m_verifyServerEdit->setText(m_revealed.at(row).serverSeed);
    m_verifyClientEdit->setText(m_revealed.at(row).clientSeed);
    m_verifyNonceSpin->setValue(m_revealed.at(row).nonces);
}

// ---------------------------------------------------------------- persistence

void FairPanel::saveSettings()
{
    ScopedSettings settings(m_settingsGroup);
    settings.beginGroup(QStringLiteral("VerifyRoll"));
    settings.setValue(QStringLiteral("serverSeed"), m_verifyServerEdit->text());
    settings.setValue(QStringLiteral("clientSeed"), m_verifyClientEdit->text());
    settings.setValue(QStringLiteral("nonce"), m_verifyNonceSpin->value());
    settings.setValue(QStringLiteral("useSeeds"), m_useSeedsCheck->isChecked());
    settings.endGroup();
}

void FairPanel::loadSettings()
{
    ScopedSettings settings(m_settingsGroup);
    settings.beginGroup(QStringLiteral("VerifyRoll"));
    const QString server = settings.value(QStringLiteral("serverSeed")).toString();
    const QString client = settings.value(QStringLiteral("clientSeed")).toString();
    const int nonce = settings.value(QStringLiteral("nonce"), 1).toInt();
    const bool useSeeds = settings.value(QStringLiteral("useSeeds"), false).toBool();
    settings.endGroup();

    {
        // Block signals so restoring doesn't fire saveSettings mid-load, and
        // doesn't auto-apply the seeds via onUseSeedsToggled before we've
        // explicitly decided whether to.
        QSignalBlocker b1(m_verifyServerEdit);
        QSignalBlocker b2(m_verifyClientEdit);
        QSignalBlocker b3(m_verifyNonceSpin);
        QSignalBlocker b4(m_useSeedsCheck);

        m_verifyServerEdit->setText(server);
        m_verifyClientEdit->setText(client);
        m_verifyNonceSpin->setValue(nonce);
        m_useSeedsCheck->setChecked(useSeeds);
    }

    // If the box was checked when we saved, push those seeds back into the
    // provider. The provider is created with fresh random seeds on launch, so
    // without this the simulation would silently diverge from the saved state.
    if (useSeeds) {
        if (!server.trimmed().isEmpty() && !client.trimmed().isEmpty()) {
            m_provider->setServerSeed(server);
            m_provider->setClientSeed(client);
            m_provider->setNonce(nonce);
        } else {
            // Corrupted save: an empty seed pair can't be used. Drop the box
            // without triggering onUseSeedsToggled(false) (which would call
            // randomizeSeeds and wipe the fresh random pair the provider
            // already has).
            QSignalBlocker blocker(m_useSeedsCheck);
            m_useSeedsCheck->setChecked(false);
        }
    }
}