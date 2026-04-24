#pragma once
#include <QString>
#include <QList>
#include <QTime>
#include <QRegularExpression>

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
    inline const int SCHEDULE_MINUTES = 0;
    inline const int POLL_INTERVAL_MS = 2500;

    // Schedule time in HH:MM format from env var, default "21:00"
    inline const QString SCHEDULE_TIME_STR = qEnvironmentVariable("SCHEDULE_TIME", "21:00");

        // Parse HH:MM into QTime for easy comparison
    inline const QTime parseScheduleTime()
    {
        QRegularExpression re(R"(^(\d{1,2}):(\d{2})$)");
        auto match = re.match(SCHEDULE_TIME_STR);
        if (match.hasMatch())
        {
            int h = match.captured(1).toInt();
            int m = match.captured(2).toInt();
            if (h >= 0 && h < 24 && m >= 0 && m < 60)
            {
                return QTime(h, m);
            }
        }

        return QTime(21, 0);
    }

    inline const QTime SCHEDULE_TIME = parseScheduleTime();
}