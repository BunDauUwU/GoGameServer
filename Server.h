#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QWebSocketServer>
#include "ByoYomiClock.h"
// AI-assisted: see AI11 in docs/AI_Declaration.md (shared rules include fix).
#include "GoMatch.h"
#include <array>
#include <memory>

class QWebSocket;

class Server : public QObject
{
public:
    explicit Server(const QString &dataFile,
                    QObject *parent = nullptr,
                    ByoYomiClock timeControl = {});
    ~Server() override;
    bool start(quint16 port, QString &error);

    void onConnection(QWebSocket *socket);
    void onMessage(QWebSocket *socket, const QString &raw);
    void onClose(QWebSocket *socket);

    void authenticate(QWebSocket *socket, QJsonObject payload);
    void startMatchmaking(QWebSocket *socket, QJsonObject payload);
    void cancelMatchmaking(QWebSocket *socket, QJsonObject payload);
    void tryCreateMatch(QString mode);
    void gameCommand(QWebSocket *socket, QJsonObject payload);

    void read(QWebSocket *client, const QString &text);
    void handler(QWebSocket *client, const QJsonObject &message);
    qint64 send(QWebSocket *client, const QJsonObject &message);

    static QString jsonToString(const QJsonObject &json);
    static bool stringToJson(const QString &text, QJsonObject &json, QString &error);
    static QJsonObject envelope(const QString &type, const QJsonObject &payload = {});

    bool readData(QJsonObject &data, QString &error) const;
    bool writeData(const QJsonObject &data, QString &error) const;
    QString dataFile() const { return m_dataFile; }

    void logServer(const QString &event, const QJsonObject &data);
    void logClient(QWebSocket *socket, const QString &type, const QJsonObject &payload);
    void removeFromQueues(QWebSocket *socket);

private:
    void acceptConnection();
    void fail(QWebSocket *client, const QString &code, const QString &message);

    QString m_dataFile;
    QWebSocketServer m_server;
    QMap<QWebSocket *, QJsonObject> m_clients;
    QMap<QString, QList<QWebSocket *>> m_queues;
    struct Match
    {
        QString id;
        QList<QWebSocket *> sockets;
        GoMatch game;
        bool started = true;
        int winnerIndex = -1;
        QString endReason;
        std::array<ByoYomiClock, 2> clocks;
        qint64 turnStartedMs = 0;
        QTimer *timer = nullptr;
    };
    void broadcast(const Match &match, const QString &type, const QJsonObject &payload);
    void sendSnapshot(const Match &match);
    void sendSnapshot(QWebSocket *socket, const Match &match);
    void endWithWinner(Match &match, int winner, const QString &reason);
    bool consumeClock(Match &match, int playerIndex);
    void armActionTimer(Match &match);
    QMap<QString, std::shared_ptr<Match>> m_matches;
    ByoYomiClock m_timeControl;
    int m_actionTimeoutMs = 30'000;
};
