#pragma once

#include "bot/BotTypes.h"
#include "bot/StakeGames.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QWidget>

class QFormLayout;

// Manual / Auto Bet tab: the settings of Stake's games other than dice / limbo, laid out like
// the C# bot's game panels (keno numbers + risk, mines count + tiles, hilo pattern + start card,
// diamonds color picker, ...), built from StakeGames::paramSpecs(). Every game keeps its own
// values while another game is selected; allParams() / setAllParams() save and restore them.
class GameParamsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GameParamsWidget(QWidget *parent = nullptr);

    void setGame(Game game);
    // Inputs this tab does not show (e.g. hilo "suitMode" on the Manual Bet tab: "Stop on" only
    // matters for runs). They keep their default value.
    void setHiddenKeys(const QStringList &keys);
    // The selected casino ("stake", "shuffle", ...): choices it does not offer are left out
    // (StakeGames::unsupportedChoices) and a saved one falls back to the default.
    void setCasino(const QString &casinoKey);
    Game game() const { return m_game; }
    // The game shows something here (inputs or a note).
    bool hasFields() const { return m_hasRows; }

    // The selected game's settings (BotConfig::gameParams).
    QJsonObject params() const;

    // {"keno": {...}, "mines": {...}, ...} for the settings file.
    QJsonObject allParams() const;
    void setAllParams(const QJsonObject &all);

signals:
    void paramsChanged();

private:
    struct Field
    {
        StakeGames::ParamSpec spec;
        QWidget *editor = nullptr;
        QWidget *rowWidget = nullptr; // what sits in the form (editor or its composite)
        QWidget *label = nullptr;     // the row's label (none for check boxes)
    };

    void rebuild();
    void storeCurrent();
    // Shows / hides the inputs that depend on another one (ParamSpec::showWhenKey).
    void updateVisibility();
    QJsonValue readEditor(const Field &field) const;
    QJsonObject valuesFor(Game game) const;

    Game m_game = Game::Dice;
    QFormLayout *m_form = nullptr;
    QList<Field> m_fields;
    QMap<QString, QJsonObject> m_values; // game name -> its settings
    bool m_building = false;
    bool m_hasRows = false;
    QStringList m_hiddenKeys;
    QString m_casino;
};
