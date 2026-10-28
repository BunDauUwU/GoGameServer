#include "Server.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QSaveFile>
#include <QUuid>
#include <QWebSocket>
#include <cmath>

#define str_(s) QStringLiteral(s)
using QJO = QJsonObject;

Server::Server(const QString &dataFile, QObject *parent, ByoYomiClock timeControl)
    : QObject(parent)
    , m_dataFile(QFileInfo(dataFile).absoluteFilePath())
    , m_server(QStringLiteral("GoGameServer"), QWebSocketServer::NonSecureMode)
{
    m_timeControl = timeControl;
    connect(&m_server, &QWebSocketServer::newConnection, this, &Server::acceptConnection);
}

Server::~Server()
{
    m_server.disconnect(this);
    for (auto socket : m_server.findChildren<QWebSocket *>())
        socket->disconnect(this);
    for (const auto &match : m_matches)
        if (match->timer) {
            match->timer->stop();
            match->timer->disconnect(this);
        }
    m_server.close();
}

bool Server::start(quint16 port, QString &error)
{
    error.clear();
    if (port == 0) {
        error = QStringLiteral("Port must be between 1 and 65535");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(m_dataFile).absolutePath())) {
        error = QStringLiteral("Cannot create storage directory");
        return false;
    }
    QJO data;
    if ((!QFileInfo::exists(m_dataFile) && !writeData({}, error)) || !readData(data, error))
        return false;
    if (!m_server.listen(QHostAddress::AnyIPv4, port)) {
        error = m_server.errorString();
        return false;
    }
    return true;
}

void Server::acceptConnection()
{
    QWebSocket *socket = m_server.nextPendingConnection();
    if (!socket)
        return;

    onConnection(socket);
}

QJO Server::envelope(const QString &type, const QJO &payload)
{
    return {{"type", type}, {"payload", payload}};
}

QString Server::jsonToString(const QJO &json)
{
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

bool Server::stringToJson(const QString &text, QJO &json, QString &error)
{
    error.clear();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Message must be a valid JSON object");
        return false;
    }
    json = document.object();
    return true;
}

qint64 Server::send(QWebSocket *client, const QJO &message)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState)
        return -1;
    return client->sendTextMessage(jsonToString(message));
}

void Server::fail(QWebSocket *client, const QString &code, const QString &message)
{
    send(client, envelope("Error", {{"code", code}, {"message", message}}));
}

void Server::read(QWebSocket *client, const QString &text)
{
    QJO message;
    QString error;
    if (!stringToJson(text, message, error)) {
        fail(client, "InvalidJson", error);
        return;
    }
    handler(client, message);
}

void Server::handler(QWebSocket *client, const QJO &message)
{
    const QString type = message.value("type").toString();
    QString error;
    const auto payloadValue = message.value("payload");
    if (type.isEmpty() || (!payloadValue.isUndefined() && !payloadValue.isObject())) {
        fail(client, "InvalidEnvelope", "type must be non-empty and payload must be an object");
        return;
    }
    const auto payload = payloadValue.toObject();
    logClient(client, type, payload);
    if (type == "AuthRequest") {
        authenticate(client, payload);
        return;
    }
    if (type == "MatchmakingStart") {
        startMatchmaking(client, payload);
        return;
    }
    if (type == "MatchmakingCancel") {
        cancelMatchmaking(client, payload);
        return;
    }
    if (type == "GameCommand") {
        gameCommand(client, payload);
        return;
    }
    if (type == "ReadData") {
        QJO data;
        if (!readData(data, error)) {
            fail(client, "ReadFailed", error);
            return;
        }
        send(client, envelope("Data", {{"data", data}}));
    } else if (type == "WriteData") {
        const auto payload = message.value("payload");
        if (!payload.isObject() || !payload.toObject().value("data").isObject()) {
            fail(client, "InvalidPayload", "payload.data must be a JSON object");
            return;
        }
        if (!writeData(payload.toObject().value("data").toObject(), error)) {
            fail(client, "WriteFailed", error);
            return;
        }
        send(client, envelope("DataWritten", {{"ok", true}}));
    } else {
        fail(client,
             "UnknownType",
             "Supported types: AuthRequest, MatchmakingStart, MatchmakingCancel, "
             "GameCommand, ReadData, WriteData");
    }
}

bool Server::writeData(const QJO &data, QString &error) const
{
    QSaveFile file(m_dataFile);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(data).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

bool Server::readData(QJO &data, QString &error) const
{
    QFile file(m_dataFile);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Data file must contain a valid JSON object");
        return false;
    }
    data = document.object();
    return true;
}

void Server::onConnection(QWebSocket *socket)
{
    socket->setParent(&m_server);
    socket->setMaxAllowedIncomingMessageSize(1024 * 1024);

    QJO clientData;

    clientData[str_("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clientData[str_("nickname")] = str_("");
    clientData[str_("matchId")] = str_("");

    m_clients.insert(socket, clientData);
    logServer(str_("client connected"), QJO{{str_("playerId"), clientData[str_("id")]}});

    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
        this->onMessage(socket, text);
    });

    // Để sau này tối ưu nma có vẻ sus lắm
    connect(socket, &QWebSocket::binaryMessageReceived, this, [this, socket](const QByteArray &) {
        this->fail(socket, str_("InvalidMessage"), str_("Binary messages are not supported"));
    });

    connect(socket, &QWebSocket::disconnected, this, [this, socket]() { this->onClose(socket); });
}

void Server::onMessage(QWebSocket *socket, const QString &raw)
{
    read(socket, raw);
}

void Server::onClose(QWebSocket *socket)
{
    if (socket) {
        this->removeFromQueues(socket);
        const QString id = m_clients.value(socket).value("matchId").toString();
        if (m_matches.contains(id)) {
            auto match = m_matches.value(id);
            if (match->started)
                endWithWinner(*match, 1 - match->sockets.indexOf(socket), "Opponent disconnected");
            m_matches.remove(id);
        }
        m_clients.remove(socket);
        socket->deleteLater();
    }
}

void Server::logServer(const QString &event, const QJsonObject &data)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

    if (data.isEmpty()) {
        qDebug().noquote() << QString("[%1] [SERVER] %2").arg(timestamp, event);
    } else {
        QString jsonStr = jsonToString(data);
        qDebug().noquote() << QString("[%1] [SERVER] %2 %3").arg(timestamp, event, jsonStr);
    }
}

void Server::logClient(QWebSocket *socket, const QString &type, const QJsonObject &payload)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString playerId = QStringLiteral("unknown");

    if (socket && m_clients.contains(socket)) {
        playerId = m_clients.value(socket).value(QStringLiteral("id")).toString();
    }

    QString payloadStr = jsonToString(payload);

    qDebug().noquote() << QString("[%1] [CLIENT -> %2] %3 %4")
                              .arg(timestamp, playerId, type, payloadStr);
}
void Server::authenticate(QWebSocket *socket, QJsonObject payload)
{
    const QString nickname = payload["nickname"].toString().trimmed();

    if (!nickname.size() || nickname.size() > 17) {
        fail(socket, str_("AuthResult"), str_("Nickname must contain 1 to 17 characters"));
        return;
    }

    if (!m_clients.contains(socket))
        return;
    QJO &client = m_clients[socket];
    client["nickname"] = nickname;

    logServer("client authenticated",
              {{"success", true}, {"playerId", client["id"]}, {"nickname", nickname}});
    send(socket,
         envelope("AuthResult",
                  {{"success", true}, {"playerId", client["id"]}, {"nickname", nickname}}));
}

void Server::startMatchmaking(QWebSocket *socket, QJsonObject payload)
{
    if (!m_clients.contains(socket))
        return;
    const QJO client = m_clients.value(socket);
    if (client["nickname"].toString().isEmpty()) {
        fail(socket, "NotAuthenticated", "Pls login");
        return;
    }
    if (!client["matchId"].toString().isEmpty()) {
        fail(socket, "AlreadyInMatch", "Get out of the match pls");
        return;
    }

    const QString mode = payload["mode"].toString().trimmed();
    if (mode.isEmpty() || mode.size() > 64) {
        fail(socket, "InvalidMode", "Mode must contain 1 to 64 characters");
        return;
    }
    removeFromQueues(socket);
    QList<QWebSocket *> &queue = m_queues[mode];
    queue.append(socket);
    m_queues.insert(mode, queue);
    send(socket,
         envelope("MatchmakingStatus",
                  {{"status", "Finding opponent..."}, {"mode", mode}, {"searching", true}}));
    tryCreateMatch(mode);
}

void Server::cancelMatchmaking(QWebSocket *socket, QJsonObject payload)
{
    removeFromQueues(socket);
    send(socket,
         envelope("MatchmakingStatus", {{"status", "Matchmaking cancelled"}, {"searching", false}}));
}

#include <QTimer>

void Server::tryCreateMatch(QString mode)
{
    if (!m_queues.contains(mode))
        return;
    auto &queue = m_queues[mode];
    for (auto it = queue.begin(); it != queue.end();) {
        auto socket = *it;
        if (!m_clients.contains(socket) || socket->state() != QAbstractSocket::ConnectedState
            || !m_clients.value(socket).value("matchId").toString().isEmpty())
            it = queue.erase(it);
        else
            ++it;
    }
    while (queue.size() >= 2) {
        auto match = std::make_shared<Match>();
        match->clocks = {m_timeControl, m_timeControl};
        match->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        match->sockets = {queue.takeFirst(), queue.takeFirst()};
        m_matches.insert(match->id, match);
        for (int i = 0; i < 2; ++i) {
            auto socket = match->sockets[i];
            m_clients[socket]["matchId"] = match->id;
            send(socket,
                 envelope("MatchFound",
                          {{"matchId", match->id},
                           {"playerIndex", i},
                           {"opponentName",
                            m_clients.value(match->sockets[1 - i]).value("nickname")}}));
        }
        armActionTimer(*match);
        sendSnapshot(*match);
    }
    if (queue.isEmpty())
        m_queues.remove(mode);
}

namespace {
qint64 monotonicMs()
{
    static const QElapsedTimer clock = [] {
        QElapsedTimer value;
        value.start();
        return value;
    }();
    return clock.elapsed();
}
} // namespace

void Server::broadcast(const Match &match, const QString &type, const QJsonObject &payload)
{
    for (auto socket : match.sockets)
        send(socket, envelope(type, payload));
}
// AI-assisted: see AI07 in docs/AI_Declaration.md (snapshot broadcast).
void Server::sendSnapshot(const Match &match)
{
    for (auto socket : match.sockets)
        sendSnapshot(socket, match);
}

// AI-assisted: see AI07 in docs/AI_Declaration.md (recipient-specific Go snapshot).
void Server::sendSnapshot(QWebSocket *socket, const Match &match)
{
    const int localIndex = match.sockets.indexOf(socket);
    if (localIndex < 0 || !m_clients.contains(socket))
        return;

    const qint64 elapsed = match.started ? qMax(qint64(0), monotonicMs() - match.turnStartedMs) : 0;
    const qint64 nowEpoch = QDateTime::currentMSecsSinceEpoch();
    QJsonArray time;
    QJsonArray clocks;
    QJsonArray players;
    for (int i = 0; i < match.sockets.size(); ++i) {
        auto clock = match.clocks[i];
        if (i == match.game.currentPlayer)
            clock.advance(elapsed);
        const qint64 remaining = clock.untilBoundary();
        time.append(remaining);
        clocks.append(clock.json());
        const auto player = m_clients.value(match.sockets[i]);
        players.append(QJO{{"playerId", player.value("id")},
                           {"nickname", player.value("nickname")},
                           {"playerIndex", i},
                           {"color", i == 0 ? "Black" : "White"},
                           {"remainingTimeMs", remaining}});
    }
    const qint64 deadline = match.started
                                ? nowEpoch + time[match.game.currentPlayer].toVariant().toLongLong()
                                : 0;
    send(socket,
         envelope("GameSnapshot",
                  {{"matchId", match.id},
                   {"stage", match.started ? "Action" : "GameEnd"},
                   {"boardSize", 19},
                   {"board", match.game.state()},
                   {"position", match.game.toJson()},
                   {"scoring",
                    match.game.scoreDetails(match.started ? GoMatch::ScoringPhase::Current
                                                          : GoMatch::ScoringPhase::Final)},
                   {"moveCount", match.game.moves},
                   {"m_passes", match.game.m_passes},
                   {"currentPlayerIndex", match.game.currentPlayer},
                   {"remainingMs", time},
                   {"clocks", clocks},
                   {"started", match.started},
                   {"komi", 6.5},
                   {"players", players},
                   {"self", QJO{{"playerIndex", localIndex}}},
                   {"serverTimeEpochMs", nowEpoch},
                   {"actionDeadlineEpochMs", deadline},
                   {"winnerIndex", match.winnerIndex},
                   {"endReason", match.endReason}}));
}
void Server::endWithWinner(Match &match, int winner, const QString &reason)
{
    if (match.started) {
        const int active = match.game.currentPlayer;
        match.clocks[active].advance(monotonicMs() - match.turnStartedMs);
    }
    match.started = false;
    match.winnerIndex = winner;
    match.endReason = reason;
    if (match.timer) {
        match.timer->stop();
        match.timer->deleteLater();
        match.timer = nullptr;
    }
    for (auto socket : match.sockets)
        if (m_clients.contains(socket))
            m_clients[socket]["matchId"] = "";
    QJO event{{"eventType", "GameEnded"}, {"winnerIndex", winner}, {"reason", reason}};
    if (match.game.m_passes >= 2)
        event["scoring"] = match.game.scoreDetails(GoMatch::ScoringPhase::Final);
    broadcast(match, "GameEvent", {{"matchId", match.id}, {"events", QJsonArray{event}}});
    sendSnapshot(match);
    m_matches.remove(match.id);
}
bool Server::consumeClock(Match &match, int playerIndex)
{
    const auto now = monotonicMs();
    const bool alive = match.clocks[playerIndex].advance(now - match.turnStartedMs);
    // match.turnStartedMs = now;
    if (alive)
        return true;
    endWithWinner(match, 1 - playerIndex, "Time expired");
    return false;
}
void Server::armActionTimer(Match &match)
{
    if (!match.timer) {
        match.timer = new QTimer(this);
        match.timer->setSingleShot(true);
        connect(match.timer, &QTimer::timeout, this, [this, id = match.id] {
            auto match = m_matches.value(id);
            if (match && match->started) {
                if (consumeClock(*match, match->game.currentPlayer)) {
                    armActionTimer(*match);
                    sendSnapshot(*match);
                }
            }
        });
    }
    match.turnStartedMs = monotonicMs();
    match.timer->start(static_cast<int>(
        qMin(qint64(m_actionTimeoutMs), match.clocks[match.game.currentPlayer].untilBoundary())));
}

void Server::gameCommand(QWebSocket *socket, QJsonObject payload)
{
    const QString id = payload.value("matchId").toString();
    auto match = m_matches.value(id);
    if (!m_clients.contains(socket) || !match || !match->started
        || m_clients.value(socket).value("matchId").toString() != id
        || !match->sockets.contains(socket)) {
        fail(socket, "MatchNotFound", "The requested match is not active");
        return;
    }
    const QString type = payload.value("commandType").toString();
    if (type != "Play" && type != "EndRound" && type != "Concede") {
        fail(socket, "InvalidCommand", "Supported commands: Play, EndRound, Concede");
        return;
    }
    const int playerIndex = match->sockets.indexOf(socket);
    if (type == "Concede") {
        endWithWinner(*match, 1 - playerIndex, "Opponent conceded");
        return;
    }
    if (playerIndex != match->game.currentPlayer) {
        fail(socket, "NotYourTurn", "Wait for the other player");
        return;
    }
    if (!consumeClock(*match, playerIndex))
        return;
    const auto commandValue = payload.value("command");
    if (!commandValue.isUndefined() && !commandValue.isObject()) {
        fail(socket, "InvalidCommand", "command must be an object");
        armActionTimer(*match);
        return;
    }
    const auto command = commandValue.toObject();
    QJO event{{"playerIndex", playerIndex}};
    if (type == "Play") {
        const auto row = command.value("row"), col = command.value("col");
        if (!row.isDouble() || !col.isDouble() || row.toDouble() != std::floor(row.toDouble())
            || col.toDouble() != std::floor(col.toDouble()) || row.toDouble() < 0
            || row.toDouble() >= 19 || col.toDouble() < 0 || col.toDouble() >= 19) {
            fail(socket, "InvalidPosition", "row and col must be integers between 0 and 18");
            armActionTimer(*match);
            return;
        }
        QString error;
        if (!match->game.play(row.toInt(), col.toInt(), error)) {
            fail(socket, "InvalidMove", error);
            armActionTimer(*match);
            return;
        }
        event["eventType"] = "StonePlayed";
        event["captured"] = match->game.capturedLastMove;
        event["koPoint"] = match->game.koPoint;
        event["koThreat"] = match->game.koThreat;
        event["row"] = row;
        event["col"] = col;
    } else {
        // == Unknown
        match->game.pass();
        event["eventType"] = "Passed";
    }
    match->clocks[playerIndex].completeMove();
    match->turnStartedMs = monotonicMs();
    broadcast(*match, "GameEvent", {{"matchId", id}, {"events", QJsonArray{event}}});
    if (match->game.m_passes == 2) {
        const auto score = match->game.score();
        endWithWinner(*match, score > 0 ? 0 : 1, QString("Two passes \n Different: %1").arg(score));
        return;
    }
    armActionTimer(*match);
    sendSnapshot(*match);
}

void Server::removeFromQueues(QWebSocket *socket)
{
    auto it = m_queues.begin();
    while (it != m_queues.end()) {
        QList<QWebSocket *> &queue = it.value();

        queue.removeAll(socket);

        if (queue.isEmpty()) {
            it = m_queues.erase(it);
        } else {
            ++it;
        }
    }
}
