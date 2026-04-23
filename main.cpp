#include <QCoreApplication>
#include <QDebug>
#include "config.h"
#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"
#include "botManager.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << "CheckOnlineBot starting...";
    qDebug() << "Qt Version:" << QT_VERSION_STR;
    qDebug() << "Destiny ID:" << Config::DESTINY_ID;
    qDebug() << "Marathon ID:" << Config::MARATHON_ID;
    qDebug() << "========================================";

    if (Config::TG_TOKEN.isEmpty())
    {
        qWarning() << "WARNING: TG_BOT_TOKEN not set. Bot will not start.";
    }

    auto* tg = new TelegramClient(Config::TG_TOKEN, &app);
    auto* steam = new SteamApi(&app);
    auto* popularity = new PopularityApi(&app);
    auto* bot = new BotManager(tg, steam, popularity, &app);

    bot->start();

    return app.exec();
}