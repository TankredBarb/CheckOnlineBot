#include <QCoreApplication>
#include <QDebug>
#include "config.h"
#include "telegramClient.h"
#include "steamApi.h"
#include "botManager.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "========================================";
    qDebug() << "SteamBot starting...";
    qDebug() << "Qt Version:" << QT_VERSION_STR;
    qDebug() << "Destiny ID:" << Config::DESTINY_ID;
    qDebug() << "Marathon ID:" << Config::MARATHON_ID;
    qDebug() << "========================================";

    if (Config::TG_TOKEN == "YOUR_TEST_TOKEN_HERE") {
        qWarning() << "WARNING: Using default test token. Set TG_BOT_TOKEN env variable!";
    }

    auto* tg = new TelegramClient(Config::TG_TOKEN, &app);
    auto* steam = new SteamApi(&app);
    auto* bot = new BotManager(tg, steam, &app);

    bot->start();

    return app.exec();
}