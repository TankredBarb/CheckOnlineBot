#include "uptimeTracker.h"

UptimeTracker::UptimeTracker()
{
    reset();
}

void UptimeTracker::reset()
{
    m_startTime = QDateTime::currentDateTimeUtc();
    m_timer.start();
}

qint64 UptimeTracker::seconds() const
{
    return m_timer.elapsed() / 1000;
}

QDateTime UptimeTracker::startTime() const
{
    return m_startTime;
}

QString UptimeTracker::toString() const
{
    qint64 totalSeconds = seconds();
    qint64 days = totalSeconds / 86400;
    totalSeconds %= 86400;
    qint64 hours = totalSeconds / 3600;
    totalSeconds %= 3600;
    qint64 minutes = totalSeconds / 60;
    qint64 remainingSeconds = totalSeconds % 60;

    if (days > 0)
    {
        return QString("%1d %2h %3m %4s")
                .arg(days)
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(remainingSeconds, 2, 10, QChar('0'));
    }

    if (hours > 0)
    {
        return QString("%1h %2m %3s")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(remainingSeconds, 2, 10, QChar('0'));
    }

    return QString("%1m %2s")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(remainingSeconds, 2, 10, QChar('0'));
}