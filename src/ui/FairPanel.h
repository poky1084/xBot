#pragma once

#include "bot/BotTypes.h"
#include "bot/SimulatedProvider.h"

#include <QJsonObject>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

class SimulatedProvider;

// "Provably Fair" tab: shows the active seed pair (server seed hidden
// behind its hash), lets you rotate seeds and verify past rolls.
class FairPanel : public QWidget
{
    Q_OBJECT

public:
    // settingsGroup: the owning main window's QSettings group (see ui/WindowSettings.h).
    explicit FairPanel(SimulatedProvider *provider, const QString &settingsGroup = QString(),
                       QWidget *parent = nullptr);

    void refresh();

    // The game the "Verify a roll" section works out (any of Stake's games: dice roll, keno
    // draw, mines, plinko path, cards, ...).
    void setGame(Game game);
    // Where the game's settings come from (the Manual Bet tab: mines count, plinko rows /
    // risk, keno numbers, ...): read every time the result is worked out.
    void setParamsSource(std::function<QJsonObject()> source);

    // Code mode's serverSeed / clientSeed / nonce: the "Verify a roll" seeds and nonce when
    // "Use these seeds for simulation" is ticked...
    bool customSeeds(QString *server, QString *client, qint64 *nonce) const;
    // ... otherwise the latest revealed seed pair (false when none was revealed yet).
    bool latestRevealed(RevealedSeeds *out) const;

public slots:
    void onRotate();
    // Works the verify result out again (the game's settings changed).
    void refreshVerify() { updateVerifyPreview(); }
    void saveSettings();

signals:
    void seedRotated(const QString &message);
    // The verify result changed size (the bottom panel can fit it again).
    void resultChanged();

private slots:
    void onVerify();
    void onPickRevealed();
    void updateVerifyPreview();
    void onUseSeedsToggled(bool checked);

private:
    void loadSettings();

    SimulatedProvider *m_provider = nullptr;
    QString m_settingsGroup;

    QLineEdit *m_serverHashEdit = nullptr;
    QLineEdit *m_clientSeedEdit = nullptr;
    QLabel *m_nonceLabel = nullptr;
    QListWidget *m_prevList = nullptr;
    QList<RevealedSeeds> m_revealed;

    QLineEdit *m_verifyServerEdit = nullptr;
    QLineEdit *m_verifyClientEdit = nullptr;
    QSpinBox *m_verifyNonceSpin = nullptr;
    QLabel *m_verifyResult = nullptr;
    QCheckBox *m_useSeedsCheck = nullptr;
    QLabel *m_verifyGame = nullptr;
    Game m_game = Game::Dice;
    std::function<QJsonObject()> m_paramsSource;
};