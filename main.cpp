#include <QCoreApplication>
#include "config.h"
#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"
#include "botManager.h"
#include "uptimeTracker.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // Initialize uptime tracker immediately upon entry
    UptimeTracker uptime;

    qDebug() << "========================================";
    qDebug() << "CheckOnlineBot starting...";
    qDebug() << "Qt Version:" << QT_VERSION_STR;
    qDebug() << "Destiny ID:" << Config::DESTINY_ID;
    qDebug() << "Marathon ID:" << Config::MARATHON_ID;
    qDebug() << "Uptime started at:" << uptime.startTime().toString(Qt::ISODate);
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