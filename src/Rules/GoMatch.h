#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <bitset>
#include <memory>
#include <vector>

class GoMatch
{
public:
    static constexpr int Size = 19;
    static constexpr int Count = Size * Size;
    enum class ScoringPhase { Current, Final };
    struct Groups
    {
        std::array<int, Count> parent;
        std::array<int, Count> size{};
        std::array<std::bitset<Count>, Count> liberties;
        int root(int pos) const
        {
            while (parent[pos] != pos)
                pos = parent[pos];
            return pos;
        }
    };
    struct RecordedMove
    {
        int player;
        int point;
    };
    struct Replay
    {
        std::array<int, Count> initialCells{};
        int initialPlayer = 0, initialKo = -1, initialPasses = 0, initialMoveCount = 0;
        std::array<int, 2> initialCaptured{};
        std::vector<RecordedMove> moves;
    };
    Replay replay() const;
    GoMatch();
    bool play(int row, int col, QString &error);
    bool validMove(int row, int col) const;
    void pass();
    double score() const;
    QJsonObject scoreDetails(ScoringPhase phase = ScoringPhase::Current) const;
    QJsonArray state() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, GoMatch &game, QString &error);
    const std::array<int, Count> &cells() const { return board; }
    const Groups &groups() const;
    int currentPlayer = 0;
    int moves = 0;
    int m_passes = 0;
    int capturedLastMove = 0;
    int koPoint = -1;
    bool koThreat = false;
    std::array<int, 2> capturedStones{};
    static const std::array<std::array<int, 4>, Count> &adj();

private:
    std::shared_ptr<Replay> m_replay;
    void recordMove(int player, int point);
    std::array<int, Count> board;
    std::array<int, Count> previousBoard;
    bool hasPrevious = false;

    mutable std::shared_ptr<const Groups> m_groups;
    // pls do not try to remove mutable I fix it for 3 hours
    static std::shared_ptr<const Groups> buildGroups(const std::array<int, Count> &board);
};
