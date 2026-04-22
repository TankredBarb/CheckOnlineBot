#pragma once
#include <QObject>
#include <QTimer>
#include "telegramClient.h"
#include "steamApi.h"

class BotManager : public QObject
{
    Q_OBJECT
public:
    explicit BotManager(TelegramClient* tg, SteamApi* steam, QObject* parent = nullptr);
    void start();

private slots:
    void onNewMessage(const TgMessage& msg);
    void onSteamDataReady(const QMap<int, int>& data);
    void scheduleTick();

private:
    void scheduleNextRun();
    qint64 msecToNextScheduledTime();
    void fetchAndBroadcast();
    QString formatReport(const QMap<int, int>& data);

    TelegramClient* m_tg;
    SteamApi* m_steam;
    QTimer m_scheduleTimer;
    qint64 m_targetChatId = 0;
    qint64 m_targetTopicId = 0; // Stores topic ID for the current pending request
    bool m_fetching = false;
};