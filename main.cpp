#include "config.h"
#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"
#include "botManager.h"
#include "uptimeTracker.h"
#include "healthServer.h"

// [+] Conditional application type
#ifdef USE_GUI_CHARTS
#include <QGuiApplication>
#else
#include <QCoreApplication>
#endif

int main(int argc, char* argv[])
{
    // [+] Create appropriate application type based on build flag
#ifdef USE_GUI_CHARTS
    QGuiApplication app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif

    // Initialize uptime tracker immediately upon entry
    UptimeTracker uptime;

    qDebug() << "========================================";
    qDebug() << "CheckOnlineBot starting...";
    qDebug() << "Qt Version:" << QT_VERSION_STR;
    qDebug() << "Destiny ID:" << Config::DESTINY_ID;
    qDebug() << "Marathon ID:" << Config::MARATHON_ID;
    qDebug() << "Uptime started at [UTC]:" << uptime.startTime().toString(Qt::ISODate);

#ifdef USE_GUI_CHARTS
    qDebug() << "Build mode: GUI charts ENABLED";
#else
    qDebug() << "Build mode: Text-only mode";
#endif

    qDebug() << "========================================";

    if (Config::TG_TOKEN.isEmpty())
    {
        qCritical() << "[FATAL] TG_BOT_TOKEN environment variable is NOT set.";
        qCritical() << "The bot cannot function without a valid Telegram token.";
        qCritical() << "Exiting...";
        return 1;
    }

    if (Config::TARGET_CHAT_ID == 0)
    {
        qWarning() << "[Config] TG_CHANNEL_ID is not set. Scheduled reports will be DISABLED.";
    }
    else
    {
        qDebug() << "[Config] Scheduled reports enabled for Chat ID:" << Config::TARGET_CHAT_ID;
        if (Config::TARGET_TOPIC_ID != 0)
            qDebug() << "[Config] Targeted Topic ID:" << Config::TARGET_TOPIC_ID;
    }

    auto* tg = new TelegramClient(Config::TG_TOKEN, &app);
    auto* steam = new SteamApi(&app);
    auto* popularity = new PopularityApi(&app);
    // Pass uptime by const reference to BotManager
    auto* bot = new BotManager(tg, steam, popularity, uptime, &app);

    if (Config::HEALTH_PORT > 0)
    {
        auto* health = new HealthServer(&app);
        if (health->listen(QHostAddress::Any, static_cast<quint16>(Config::HEALTH_PORT)))
        {
            qDebug() << "[Health] HTTP server started on port" << Config::HEALTH_PORT;
        }
        else
        {
            qWarning() << "[Health] FAILED to start server on port" << Config::HEALTH_PORT << ":" << health->errorString();
        }
    }
    else
    {
        qDebug() << "[Health] Server disabled (HEALTH_PORT not set).";
    }

    bot->start();

    return app.exec();
}