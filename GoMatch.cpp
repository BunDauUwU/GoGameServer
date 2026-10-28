#include "GoMatch.h"
#include <vector>
#include <set>

namespace {
std::vector<int> neighbors(int pos) {
    std::vector<int> result;
    if (pos / 19 > 0) result.push_back(pos - 19);
    if (pos / 19 < 18) result.push_back(pos + 19);
    if (pos % 19 > 0) result.push_back(pos - 1);
    if (pos % 19 < 18) result.push_back(pos + 1);
    return result;
}
std::vector<int> group(const std::array<int,361> &board, int pos, bool &hasLiberty) {
    std::vector<int> cells{pos};
    std::set<int> visited{pos};
    hasLiberty = false;
    for (size_t i = 0; i < cells.size(); ++i) {
        for (int next : neighbors(cells[i])) {
            if (board[next] == -1) hasLiberty = true;
            else if (board[next] == board[pos] && visited.insert(next).second)
                cells.push_back(next);
        }
    }
    return cells;
}
}
GoMatch::GoMatch() { board.fill(-1); previousBoard.fill(-1); }
bool GoMatch::play(int row, int col, QString &error) {
    error.clear();
    if (row < 0 || row >= 19 || col < 0 || col >= 19) { error = "OutsideBoard"; return false; }
    int pos = row * 19 + col;
    if (board[pos] != -1) { error = "Occupied"; return false; }
    auto candidate = board;
    candidate[pos] = currentPlayer;
    for (int next : neighbors(pos)) {
        if (candidate[next] != 1 - currentPlayer) continue;
        bool liberty;
        auto stones = group(candidate, next, liberty);
        if (!liberty) for (int stone : stones) candidate[stone] = -1;
    }
    bool liberty;
    group(candidate, pos, liberty);
    if (!liberty) { error = "Suicide"; return false; }
    if (hasPrevious && candidate == previousBoard) { error = "Ko"; return false; }
    previousBoard = board;
    hasPrevious = true;
    board = candidate;
    currentPlayer = 1 - currentPlayer;
    consecutivePasses = 0;
    ++moveCount;
    return true;
}
void GoMatch::pass() {
    previousBoard = board;
    hasPrevious = true;
    ++moveCount;
    ++consecutivePasses;
    currentPlayer = 1 - currentPlayer;
}
QJsonArray GoMatch::state() const {
    QJsonArray result;
    for (int color : board) result.append(color);
    return result;
}
double GoMatch::score() const {
    double result = -6.5;
    std::set<int> visited;
    for (int pos = 0; pos < 361; ++pos) {
        if (board[pos] != -1) { result += board[pos] == 0 ? 1 : -1; continue; }
        if (!visited.insert(pos).second) continue;
        std::vector<int> region{pos};
        std::set<int> borders;
        for (size_t i = 0; i < region.size(); ++i) {
            for (int next : neighbors(region[i])) {
                if (board[next] == -1) {
                    if (visited.insert(next).second) region.push_back(next);
                } else borders.insert(board[next]);
            }
        }
        if (borders.size() == 1) result += *borders.begin() == 0 ? region.size() : -static_cast<double>(region.size());
    }
    return result;
}
