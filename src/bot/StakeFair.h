#pragma once

#include "bot/BotTypes.h"
#include "bot/StakeTransport.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

class SimulatedProvider;

// Stake's provably fair games, worked out from a seed pair exactly as Stake describes them on
// its "Provably Fair" pages:
//   bytes  = HMAC_SHA256(serverSeed, "clientSeed:nonce:round"), round 0, 1, 2, ... (32 bytes each)
//   floats = every 4 bytes -> b0/256 + b1/256^2 + b2/256^3 + b3/256^4
//   game events = float * possible outcomes (cards, tiles, directions, ...); games whose events
//   cannot repeat (keno, mines, video poker, ...) use a Fisher-Yates shuffle.
// Used by the Fairness tab (verify a bet of any game) and by the simulation (play Stake's games
// without money, see SimulatedStakeTransport).
namespace StakeFair
{
struct Seeds
{
    QString server;
    QString client;
    qint64 nonce = 1;
};

// `count` floats of the bet, starting at byte `cursor` (a multiple of 4).
QVector<double> floats(const Seeds &seeds, int count, int cursor = 0);
// Fisher-Yates: the first `picks` of 0 .. pool-1, the n-th one chosen with floats[n].
QVector<int> shuffled(const QVector<double> &floats, int pool, int picks);

// Cards: index 0 .. 51 = ♦2, ♥2, ♠2, ♣2, ♦3, ... ♣A (Stake's order); {rank, suit} as the
// casino answers them ("10", "H").
int cardIndex(double f);
QJsonObject cardJson(int index);
QString cardText(int index);          // "♥10"

// Payout tables (multiplier of the stake). `ok` = false when the table is not known.
double kenoMultiplier(const QString &risk, int picks, int hits, bool *ok = nullptr);
double plinkoMultiplier(int rows, const QString &risk, int slot, bool *ok = nullptr);
double wheelMultiplier(int segments, const QString &risk, int index, bool *ok = nullptr);
double minesMultiplier(int mines, int revealed);

// Can the simulation play `game` (and these settings)? `why` says why not.
bool canSimulate(Game game, QString *why = nullptr);

// The Fairness tab: what the seed pair + nonce give for `game` (HTML), with the game's
// settings `params` (Manual Bet tab: mines count, plinko rows / risk, keno numbers, ...).
QString verifyHtml(Game game, const Seeds &seeds, const QJsonObject &params);
} // namespace StakeFair

// The simulation's "Stake": answers the same REST / GraphQL requests StakeClient sends to Stake
// for its games (StakeGames::buildBetRequest() / nextAction()) with bets in Stake's own shape,
// worked out from the simulation's seed pair (SimulatedProvider). Each new bet takes the next
// nonce; a hilo / blackjack / video poker / mines round stays open until it is finished, like on
// Stake. So the engine, the Manual Bet tab, the scripts' round() and the bet list play these
// games in simulation exactly as they do live.
class SimulatedStakeTransport final : public IStakeTransport
{
public:
    explicit SimulatedStakeTransport(SimulatedProvider *provider, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("Simulation"); }
    bool isReady(QString *whyNot = nullptr) const override;
    void post(const QString &path, const QJsonObject &body, TransportCallback cb) override;

    // Forgets every open round (session reset).
    void clearOpenGames() { m_open.clear(); }

    struct Hand
    {
        QVector<int> cards;
        QJsonArray actions;
        bool doubled = false;
        bool done = false;
    };
    struct Round
    {
        Game game = Game::Dice;
        StakeFair::Seeds seeds;
        double amount = 0.0;  // the stake (blackjack: of one hand)
        QString id;
        int next = 0;         // next card / float of the bet's stream
        // hilo
        QJsonObject startCard;
        QJsonArray rounds;
        double multiplier = 1.0;
        int lastRank = 0;     // 1 (A) .. 13 (K)
        int guesses = 0;      // guesses other than skip
        // mines
        QVector<int> mines;
        QJsonArray minesRounds;
        // video poker
        QVector<int> deck;
        // blackjack
        QVector<Hand> hands;
        QVector<int> dealer;  // dealt dealer cards (the hole card is dealer[1])
        int current = 0;
        bool insurancePending = false;
        bool insured = false;
    };

private:
    QJsonObject handle(const QString &path, const QJsonObject &body);
    StakeFair::Seeds takeSeeds();

    QJsonObject keno(const QJsonObject &b);
    QJsonObject minesBet(const QJsonObject &b);
    QJsonObject minesCashout();
    QJsonObject plinko(const QJsonObject &vars);
    QJsonObject flip(const QJsonObject &vars);
    QJsonObject wheel(const QJsonObject &b);
    QJsonObject roulette(const QJsonObject &b);
    QJsonObject baccarat(const QJsonObject &b);
    QJsonObject dragonTower(const QJsonObject &b);
    QJsonObject diamonds(const QJsonObject &b);
    QJsonObject drill(const QJsonObject &b);
    QJsonObject primedice(const QJsonObject &b);
    QJsonObject crossing(Game game, const QJsonObject &b); // pump / chicken
    QJsonObject moles(const QJsonObject &b);
    QJsonObject rps(const QJsonObject &b);
    QJsonObject snakes(const QJsonObject &b);
    QJsonObject packs(const QJsonObject &b);
    QJsonObject weighted(Game game, const QJsonObject &b); // cases / tarot / darts / bars
    QJsonObject slotGame(Game game, const QJsonObject &b);    // scarab spin / tome of life / blue samurai
    QJsonObject hiloBet(const QJsonObject &b);
    QJsonObject hiloNext(const QJsonObject &b);
    QJsonObject hiloCashout();
    QJsonObject videoPokerBet(const QJsonObject &b);
    QJsonObject videoPokerNext(const QJsonObject &b);
    QJsonObject blackjackBet(const QJsonObject &b);
    QJsonObject blackjackNext(const QJsonObject &b);
    QJsonObject activeBet(const QString &slug);

    // The open round's bet in Stake's shape.
    QJsonObject hiloJson(const Round &r, bool active, double mult) const;
    QJsonObject blackjackJson(const Round &r) const;
    QJsonObject finishBlackjack(Round r); // a copy: the open round is removed
    QJsonObject blackjackAdvance(Round &r);

    SimulatedProvider *m_provider = nullptr;
    QHash<QString, Round> m_open; // "hilo", "blackjack", "videoPoker", "mines"
    qint64 m_betId = 0;
};
