#pragma once
#include <QString>
#include <QList>
#include <QCoreApplication>

namespace Config
{
    // Read token from environment variable, fallback to default for local testing
    inline const QString TG_TOKEN = qEnvironmentVariable("TG_BOT_TOKEN", "YOUR_TEST_TOKEN_HERE");

    inline const int DESTINY_ID = 1085660;
    inline const int MARATHON_ID = 3065800;
    inline const QList<int> STEAM_APP_IDS = {DESTINY_ID, MARATHON_ID};

    inline const QString KYIV_TIMEZONE = "Europe/Kyiv";
    inline const int SCHEDULE_HOUR = 21;
    inline const int POLL_INTERVAL_MS = 2500;

    // Target Chat ID (Supergroup/Channel). Format: -100xxxxxxxxxx
    inline const qint64 TARGET_CHAT_ID = -1002476935267;

    // Topic ID (Required for Forums/Supergroups with topics enabled).
    // Set to 0 if using a standard Channel or Group without topics.
    inline const qint64 TARGET_TOPIC_ID = 32070;
}