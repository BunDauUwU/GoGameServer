#pragma once
#include <array>
#include <QJsonArray>
#include <QString>

class GoMatch {
public:
    GoMatch();
    bool play(int row, int col, QString &error);
    void pass();
    double score() const;
    QJsonArray state() const;
    int currentPlayer = 0;
    int moveCount = 0;
    int consecutivePasses = 0;
private:
    std::array<int, 361> board;
    std::array<int, 361> previousBoard;
    bool hasPrevious = false;
};
