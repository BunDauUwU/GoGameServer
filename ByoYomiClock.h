#pragma once
#include <QJsonObject>
#include <QtGlobal>

struct ByoYomiClock
{
    qint64 mainRemainingMs = 600000;
    qint64 periodDurationMs = 20000;
    qint64 periodRemainingMs = 20000;
    int periodsRemaining = 3;

    bool advance(qint64 elapsed)
    {
        elapsed = qMax(qint64(0), elapsed);
        const auto mainUsed = qMin(mainRemainingMs, elapsed);
        mainRemainingMs -= mainUsed;
        elapsed -= mainUsed;
        if (mainRemainingMs > 0)
            return true;
        while (periodsRemaining > 0 && elapsed >= periodRemainingMs) {
            elapsed -= periodRemainingMs;
            --periodsRemaining;
            periodRemainingMs = periodsRemaining > 0 ? periodDurationMs : 0;
        }
        if (periodsRemaining == 0)
            return false;
        periodRemainingMs -= elapsed;
        return true;
    }
    void completeMove()
    {
        if (mainRemainingMs == 0 && periodsRemaining > 0)
            periodRemainingMs = periodDurationMs;
    }
    qint64 untilBoundary() const
    {
        return mainRemainingMs > 0 ? mainRemainingMs : periodRemainingMs;
    }
    QJsonObject json() const
    {
        return {{"mainRemainingMs", mainRemainingMs},
                {"periodRemainingMs", periodRemainingMs},
                {"periodDurationMs", periodDurationMs},
                {"periodsRemaining", periodsRemaining},
                {"phase", mainRemainingMs > 0 ? "Main" : "ByoYomi"}};
    }
};
