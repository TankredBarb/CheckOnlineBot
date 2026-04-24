#pragma once

#include <QElapsedTimer>
#include <QDateTime>
#include <QString>

class UptimeTracker
{
public:
    UptimeTracker();

    // Resets the tracker to the current time
    void reset();

    // Returns elapsed time in seconds
    qint64 seconds() const;

    // Returns the exact UTC timestamp of the last reset
    QDateTime startTime() const;

    // Returns human-readable formatted string (e.g. "1d 04h 22m 05s")
    QString toString() const;

private:
    QElapsedTimer m_timer;
    QDateTime m_startTime;
};