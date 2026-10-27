#pragma once

#include "bot/BotTypes.h"
#include "bot/SimulatedProvider.h"

#include <QWidget>

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

    // Dice shows the roll, Limbo shows the multiplier.
    void setGame(Game game);

public slots:
    void onRotate();
    void saveSettings();

signals:
    void seedRotated(const QString &message);

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
    Game m_game = Game::Dice;
};