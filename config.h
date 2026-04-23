#pragma once
#include <QString>
#include <QList>
#include <QCoreApplication>

namespace Config
{
    // --- Security & Config ---
    // Read token from environment variable. Fallback to empty string if not set.
    inline const QString TG_TOKEN = qEnvironmentVariable("TG_BOT_TOKEN", "");

    // Channel/Topic IDs are now loaded from environment variables for security.
    // If not set, they default to 0. Bot will only reply in DMs where commanded.
    inline const qint64 TARGET_CHAT_ID = qEnvironmentVariable("TG_CHANNEL_ID").toLongLong();
    inline const qint64 TARGET_TOPIC_ID = qEnvironmentVariable("TG_TOPIC_ID").toLongLong();

    // --- Game IDs ---
    inline const int DESTINY_ID = 1085660;
    inline const int MARATHON_ID = 3065800;
    inline const QList<int> STEAM_APP_IDS = {DESTINY_ID, MARATHON_ID};

    // --- Behavior ---
    inline const QString KYIV_TIMEZONE = "Europe/Kyiv";
    inline const int SCHEDULE_HOUR = 21;
    inline const int POLL_INTERVAL_MS = 2500;
}