#include "config.h"
#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"
#include "botManager.h"
#include "uptimeTracker.h"

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
        qWarning() << "WARNING: TG_BOT_TOKEN not set. Bot will not start.";
    }

    auto* tg = new TelegramClient(Config::TG_TOKEN, &app);
    auto* steam = new SteamApi(&app);
    auto* popularity = new PopularityApi(&app);
    // Pass uptime by const reference to BotManager
    auto* bot = new BotManager(tg, steam, popularity, uptime, &app);

    bot->start();

    return app.exec();
}