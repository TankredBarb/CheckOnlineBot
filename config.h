#pragma once
#include <QString>
#include <QList>

namespace Config
{
    // --- Security & Config ---
    inline const QString TG_TOKEN = qEnvironmentVariable("TG_BOT_TOKEN", "");
    inline const qint64 TARGET_CHAT_ID = qEnvironmentVariable("TG_CHANNEL_ID").toLongLong();
    inline const qint64 TARGET_TOPIC_ID = qEnvironmentVariable("TG_TOPIC_ID").toLongLong();

    // --- Game IDs (Steam) ---
    inline const int DESTINY_ID = 1085660;
    inline const int MARATHON_ID = 3065800;
    inline const QList<int> STEAM_APP_IDS = {DESTINY_ID, MARATHON_ID};

    // --- Popularity Report API (Destiny 2 ONLY) ---
    inline const QString POPULARITY_API_KEY = qEnvironmentVariable("POPULARITY_API_KEY", "");
    inline const QString POPULARITY_API_URL = "https://api.popularity.report/general";
    inline const QString DESTINY_SLUG = "destiny-2";

    // --- Headers to mimic browser (required by Popularity API) ---
    inline const QString USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/147.0.0.0 Safari/537.36";
    inline const QString SEC_CH_UA = R"("Brave";v="147", "Not.A/Brand";v="8", "Chromium";v="147")";

    // --- Behavior ---
    inline const QString KYIV_TIMEZONE = "Europe/Kyiv";
    inline const int SCHEDULE_HOUR = 21;
    inline const int POLL_INTERVAL_MS = 2500;
}