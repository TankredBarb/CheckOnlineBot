#pragma once
#include <QObject>
#include <QTimer>
#include <QMap>
#include "telegramClient.h"
#include "steamApi.h"

// Structure to hold the destination context for a specific request
struct RequestContext
{
    qint64 chatId = 0;
    qint64 topicId = 0;
};

class BotManager : public QObject
{
    Q_OBJECT
public:
    explicit BotManager(TelegramClient* tg, SteamApi* steam, QObject* parent = nullptr);
    void start();

private slots:
    void onNewMessage(const TgMessage& msg);
    void onSteamDataReady(const QMap<int, int>& data, int requestId);
    void scheduleTick();

private:
    void scheduleNextRun();
    qint64 msecToNextScheduledTime();
    void fetchAndBroadcast(const RequestContext& context);

    QString formatReport(const QMap<int, int>& data);

    TelegramClient* m_tg;
    SteamApi* m_steam;
    QTimer m_scheduleTimer;

    // Map to track active requests: RequestID -> Destination Context
    QMap<int, RequestContext> m_pendingRequests;
    int m_requestCounter = 0;

    bool m_fetching = false;
};