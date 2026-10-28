#include "GoMatch.h"
#include <cmath>

const std::array<std::array<int, 4>, GoMatch::Count> &GoMatch::adj()
{
    static const auto ans = [] {
        std::array<std::array<int, 4>, Count> res;
        for (int i = 0; i < Count; ++i)
            res[i] = {i / Size > 0 ? i - Size : -1,
                      i / Size < Size - 1 ? i + Size : -1,
                      i % Size > 0 ? i - 1 : -1,
                      i % Size < Size - 1 ? i + 1 : -1};
        return res;
    }();
    return ans;
}
GoMatch::GoMatch()
{
    board.fill(-1);
    previousBoard.fill(-1);
}
std::shared_ptr<const GoMatch::Groups> GoMatch::buildGroups(const std::array<int, Count> &board)
{
    auto groups = std::make_shared<Groups>();
    for (int i = 0; i < Count; ++i) {
        groups->parent[i] = i;
        groups->size[i] = board[i] >= 0 ? 1 : 0;
    }

    auto root = [&](int pos) {
        while (groups->parent[pos] != pos) {
            groups->parent[pos] = groups->parent[groups->parent[pos]];
            pos = groups->parent[pos];
        }
        return pos;
    };

    for (int i = 0; i < Count; ++i) {
        if (board[i] >= 0) {
            for (int next : adj()[i])
                if (next > i && board[next] == board[i]) {
                    int a = root(i), b = root(next);
                    if (a == b)
                        continue;
                    if (groups->size[a] < groups->size[b])
                        std::swap(a, b);
                    groups->parent[b] = a;
                    groups->size[a] += groups->size[b];
                }
        }
    }

    for (int i = 0; i < Count; ++i)
        if (board[i] >= 0) {
            int r = root(i);
            groups->parent[i] = r;
            for (int next : adj()[i])
                if (next >= 0 && board[next] == -1)
                    groups->liberties[r].set(next);
        }
    return groups;
}
const GoMatch::Groups &GoMatch::groups() const
{
    if (!m_groups)
        m_groups = buildGroups(board);
    return *m_groups;
}
bool GoMatch::validMove(int row, int col) const
{
    auto tmp = *this;
    QString error;
    return tmp.play(row, col, error);
}
GoMatch::Replay GoMatch::replay() const
{
    if (m_replay)
        return *m_replay;
    Replay r;
    r.initialCells = board;
    r.initialPlayer = currentPlayer;
    r.initialKo = koPoint;
    r.initialPasses = m_passes;
    r.initialMoveCount = moves;
    r.initialCaptured = capturedStones;
    return r;
}
void GoMatch::recordMove(int player, int point)
{
    if (!m_replay)
        m_replay = std::make_shared<Replay>(replay());
    else if (!m_replay.unique())
        m_replay = std::make_shared<Replay>(*m_replay);
    m_replay->moves.push_back({player, point});
}
bool GoMatch::play(int row, int col, QString &error)
{
    error.clear();
    if (row < 0 || row >= Size || col < 0 || col >= Size) {
        error = "OutsideBoard";
        return false;
    }
    int pos = row * Size + col;
    if (pos == koPoint) {
        error = "Ko";
        return false;
    }
    if (board[pos] != -1) {
        error = "Occupied";
        return false;
    }
    const auto &tmp = groups();
    std::array<bool, Count> captured{};
    for (int next : adj()[pos])
        if (next >= 0 && board[next] == 1 - currentPlayer) {
            int r = tmp.root(next);
            if (tmp.liberties[r].count() == 1 && tmp.liberties[r].test(pos))
                captured[r] = true;
        }
    auto candidate = board;
    candidate[pos] = currentPlayer;
    int cnt = 0, last = -1;
    bool ck = false; // check captured

    for (int next : adj()[pos]) {
        if (next >= 0 && captured[tmp.root(next)]){
            ck = true;
            break;
        }
    }

    if (ck) {
        for (int i = 0; i < Count; ++i) {
            if (board[i] >= 0 && captured[tmp.root(i)]) {
                candidate[i] = -1;
                ++cnt;
                last = i;
            }
        }
    }
    auto newGroups = buildGroups(candidate);
    int root = newGroups->root(pos);
    if (newGroups->liberties[root].none()) {
        error = "Suicide";
        return false;
    }
    if (hasPrevious && candidate == previousBoard) {
        error = "Ko";
        return false;
    }
    bool threat = false;
    if (koPoint >= 0)
        for (int next : adj()[pos])
            if (next >= 0 && board[next] == 1 - currentPlayer && candidate[next] >= 0) {
                int oldRoot = tmp.root(next), newRoot = newGroups->root(next);
                if (tmp.liberties[oldRoot].count() > 1
                    && newGroups->liberties[newRoot].count() == 1)
                    threat = true;
            }
    koThreat = threat;
    recordMove(currentPlayer, pos);
    previousBoard = board;
    hasPrevious = true;
    board = candidate;
    m_groups = std::move(newGroups);
    capturedLastMove = cnt;
    capturedStones[currentPlayer] += cnt;
    koPoint = cnt == 1 && m_groups->size[root] == 1 && m_groups->liberties[root].count() == 1 ? last
                                                                                              : -1;
    currentPlayer = 1 - currentPlayer;
    m_passes = 0;
    ++moves;
    return true;
}
void GoMatch::pass()
{
    recordMove(currentPlayer, Count);
    previousBoard = board;
    hasPrevious = true;
    capturedLastMove = 0;
    koPoint = -1;
    koThreat = false;
    ++moves;
    ++m_passes;
    currentPlayer = 1 - currentPlayer;
}
QJsonArray GoMatch::state() const
{
    QJsonArray res;
    for (int color : board)
        res.append(color);
    return res;
}
QJsonObject GoMatch::scoreDetails(ScoringPhase phase) const
{
    std::array<int, Count> parent, size;
    std::array<unsigned char, Count> borders{};
    std::array<int, 2> territory{};
    for (int i = 0; i < Count; ++i) {
        parent[i] = i;
        size[i] = 1;
    }
    auto root = [&](int pos) {
        while (parent[pos] != pos) {
            parent[pos] = parent[parent[pos]];
            pos = parent[pos];
        }
        return pos;
    };
    for (int pos = 0; pos < Count; ++pos)
        if (board[pos] == -1)
            for (int next : adj()[pos])
                if (next > pos && board[next] == -1) {
                    int a = root(pos), b = root(next);
                    if (a == b)
                        continue;
                    if (size[a] < size[b])
                        std::swap(a, b);
                    parent[b] = a;
                    size[a] += size[b];
                }
    int unsettledPoints = 0;
    if (phase == ScoringPhase::Final) {
        for (int pos = 0; pos < Count; ++pos)
            if (board[pos] == -1)
                for (int next : adj()[pos])
                    if (next >= 0 && board[next] >= 0)
                        borders[root(pos)] |= 1 << board[next];
        for (int pos = 0; pos < Count; ++pos)
            if (board[pos] == -1 && root(pos) == pos) {
                if (borders[pos] == 1)
                    territory[0] += size[pos];
                else if (borders[pos] == 2)
                    territory[1] += size[pos];
                else
                    unsettledPoints += size[pos];
            }
    } else {
        auto &chains = groups();
        std::array<std::bitset<Count>, Count> regionCells, boundaryChains, vitalChains;
        std::bitset<Count> aliveChains, activeRegions;
        for (int pos = 0; pos < Count; ++pos) {
            if (board[pos] >= 0) {
                aliveChains.set(chains.root(pos));
                continue;
            }
            const int region = root(pos);
            regionCells[region].set(pos);
            for (int next : adj()[pos])
                if (next >= 0 && board[next] >= 0) {
                    borders[region] |= 1 << board[next];
                    boundaryChains[region].set(chains.root(next));
                }
        }
        for (int region = 0; region < Count; ++region) {
            if (regionCells[region].none() || (borders[region] != 1 && borders[region] != 2))
                continue;
            activeRegions.set(region);
            for (int chain = 0; chain < Count; ++chain)
                if (boundaryChains[region].test(chain)
                    && (regionCells[region] & ~chains.liberties[chain]).none())
                    vitalChains[region].set(chain);
        }
        bool changed;
        do {
            changed = false;
            std::array<int, Count> eyes{};
            for (int region = 0; region < Count; ++region) {
                if (!activeRegions.test(region))
                    continue;
                if ((boundaryChains[region] & ~aliveChains).any()) {
                    activeRegions.reset(region);
                    changed = true;
                    continue;
                }
                for (int chain = 0; chain < Count; ++chain)
                    if (vitalChains[region].test(chain))
                        ++eyes[chain];
            }
            for (int chain = 0; chain < Count; ++chain)
                if (aliveChains.test(chain) && eyes[chain] < 2) {
                    aliveChains.reset(chain);
                    changed = true;
                }
        } while (changed);
        for (int region = 0; region < Count; ++region) {
            if (regionCells[region].none())
                continue;
            if (activeRegions.test(region) && (vitalChains[region] & aliveChains).any())
                territory[borders[region] == 1 ? 0 : 1] += size[region];
            else
                unsettledPoints += size[region];
        }
    }
    const double blackPoints = territory[0] + capturedStones[0];
    const double whitePoints = territory[1] + capturedStones[1] + 6.5;
    return {{"rules", "Japanese"},
            {"phase", phase == ScoringPhase::Final ? "final" : "current"},
            {"scoreBasis", phase == ScoringPhase::Final ? "EnclosedTerritory" : "SecuredTerritory"},
            {"unsettledPoints", unsettledPoints},
            {"blackTerritory", territory[0]},
            {"whiteTerritory", territory[1]},
            {"blackPrisoners", capturedStones[0]},
            {"whitePrisoners", capturedStones[1]},
            {"blackPoints", blackPoints},
            {"whitePoints", whitePoints},
            {"komi", 6.5},
            {"provisional", true},
            {"difference", blackPoints - whitePoints}};
}
double GoMatch::score() const
{
    return scoreDetails(ScoringPhase::Final)["difference"].toDouble();
}
QJsonObject GoMatch::toJson() const
{
    QJsonArray previous;
    for (int color : previousBoard)
        previous.append(color);
    const auto r = replay();
    QJsonArray initial, moves;
    for (int c : r.initialCells)
        initial.append(c);
    for (const auto &m : r.moves)
        moves.append(QJsonArray{m.player, m.point});
    QJsonObject history{{"initialCells", initial},
                        {"initialPlayer", r.initialPlayer},
                        {"initialKo", r.initialKo},
                        {"initialPasses", r.initialPasses},
                        {"initialmoves", r.initialMoveCount},
                        {"initialCaptured", QJsonArray{r.initialCaptured[0], r.initialCaptured[1]}},
                        {"moves", moves}};
    return {{"history", history},
            {"board", state()},
            {"previousBoard", previous},
            {"hasPrevious", hasPrevious},
            {"currentPlayertmp", currentPlayer},
            {"moves", this->moves},
            {"m_passes", m_passes},
            {"capturedLastMove", capturedLastMove},
            {"capturedStones", QJsonArray{capturedStones[0], capturedStones[1]}},
            {"koPoint", koPoint},
            {"koThreat", koThreat}};
}
bool GoMatch::fromJson(const QJsonObject &json, GoMatch &game, QString &error)
{
    QJsonObject position = json;
    if (!position.contains("currentPlayertmp"))
        position["currentPlayertmp"] = position.value("currentPlayerIndex");
    if (!position.contains("moves"))
        position["moves"] = position.value("moveCount");
    else if (position["moves"].isArray() && position["history"].isObject()) {
        const auto h = position["history"].toObject();
        const auto initial = h.contains("initialmoves") ? h.value("initialmoves")
                                                        : h.value("initialMoveCount");
        if (initial.isDouble() && h["moves"].isArray() && position["moves"] == h["moves"])
            position["moves"] = initial.toDouble() + h["moves"].toArray().size();
    }
    if (!position.contains("m_passes"))
        position["m_passes"] = position.value("consecutivePasses");
    if (!position.contains("koThreat"))
        position["koThreat"] = position.value("koThreatLastMove");
    GoMatch res;
    auto array = [&](const QString &key, std::array<int, Count> &target) {
        auto data = position.value(key).toArray();
        if (data.size() != Count)
            return false;
        for (int i = 0; i < Count; ++i) {
            double v = data[i].toDouble(-2);
            if (!data[i].isDouble() || (v != -1 && v != 0 && v != 1))
                return false;
            target[i] = int(v);
        }
        return true;
    };
    auto isInt = [&](const QString &key, int &target, int min, int max) {
        const auto value = position.value(key);
        double v = value.toDouble(-1e12);
        if (!value.isDouble() || v != std::floor(v) || v < min || v > max)
            return false;
        target = int(v);
        return true;
    };
    if (!array("board", res.board) || !array("previousBoard", res.previousBoard)
        || !position.value("hasPrevious").isBool()
        || !isInt("currentPlayertmp", res.currentPlayer, 0, 1)
        || !isInt("moves", res.moves, 0, 1000000) || !isInt("m_passes", res.m_passes, 0, 2)
        || !isInt("capturedLastMove", res.capturedLastMove, 0, Count)
        || !isInt("koPoint", res.koPoint, -1, Count - 1)) {
        error = "Invalid Go position";
        return false;
    }
    auto captured = position.value("capturedStones").toArray();
    if (captured.size() != 2) {
        error = "Invalid capture counts";
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        double v = captured[i].toDouble(-1);
        if (!captured[i].isDouble() || v < 0 || v > 1000000 || v != std::floor(v)) {
            error = "Invalid capture counts";
            return false;
        }
        res.capturedStones[i] = int(v);
    }
    res.koThreat = position.value("koThreat").toBool();
    res.hasPrevious = position.value("hasPrevious").toBool();
    res.m_groups = buildGroups(res.board);
    if (position.contains("history")) {
        if (!position["history"].isObject()) {
            error = "Invalid Go history setup";
            return false;
        }
        const auto h = position.value("history").toObject();
        const auto initial = h.value("initialCells").toArray();
        QJsonObject setup = position;
        setup.remove("history");
        setup["board"] = initial;
        setup["previousBoard"] = initial;
        setup["hasPrevious"] = false;
        setup["currentPlayertmp"] = h.value("initialPlayer");
        setup["moves"] = h.contains("initialmoves") ? h.value("initialmoves")
                                                   : h.value("initialMoveCount");
        setup["m_passes"] = h.value("initialPasses");
        setup["capturedStones"] = h.value("initialCaptured");
        setup["capturedLastMove"] = 0;
        setup["koPoint"] = h.value("initialKo");
        GoMatch match;
        if (!fromJson(setup, match, error) || !h.value("moves").isArray()) {
            error = "Invalid Go history setup";
            return false;
        }
        const auto moves = h.value("moves").toArray();
        if (moves.size() > 100000) {
            error = "Go history too long";
            return false;
        }
        for (const auto &item : moves) {
            const auto move = item.toArray();
            if (move.size() != 2 || !move[0].isDouble() || !move[1].isDouble()
                || move[0].toDouble() != match.currentPlayer
                || move[1].toDouble() != std::floor(move[1].toDouble()) || move[1].toInt(-1) < 0
                || move[1].toInt() > Count) {
                error = "Invalid Go history move";
                return false;
            }
            const int point = move[1].toInt();
            if (point == Count)
                match.pass();
            else if (!match.play(point / Size, point % Size, error))
                return false;
        }
        if (match.board != res.board || match.currentPlayer != res.currentPlayer
            || match.moves != res.moves || match.koPoint != res.koPoint
            || match.capturedStones != res.capturedStones || match.m_passes != res.m_passes
            || (!moves.empty()
                && (match.previousBoard != res.previousBoard
                    || match.hasPrevious != res.hasPrevious))) {
            error = "Go history does not match position";
            return false;
        }
        res.m_replay = match.m_replay;
    }
    game = std::move(res);
    error.clear();
    return true;
}
