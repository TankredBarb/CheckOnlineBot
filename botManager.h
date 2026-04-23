#pragma once
#include <QObject>
#include <QTimer>
#include <QMap>
#include <QString>
#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"

struct RequestContext
{
    qint64 chatId = 0;
    qint64 topicId = 0;
};

class BotManager : public QObject
{
    Q_OBJECT
public:
    explicit BotManager(TelegramClient* tg, SteamApi* steam, PopularityApi* popularity, QObject* parent = nullptr);
    void start();

private slots:
    void onNewMessage(const TgMessage& msg);
    void onSteamDataReady(const QMap<int, int>& data, const QString& error, int requestId);
    void onPopularityDataReady(int players, const QString& error, QString gameSlug, int requestId);
    void scheduleTick();

private:
    void scheduleNextRun();
    qint64 msecToNextScheduledTime();
    void fetchAndBroadcast(const RequestContext& context);
    void checkAndSend(int requestId);
    void sendReport(int requestId);
    QString formatReport(const QMap<int, int>& steamData, const QString& steamError,
                         int destinyAllPlatforms, const QString& popError);

    TelegramClient* m_tg;
    SteamApi* m_steam;
    PopularityApi* m_popularity;
    QTimer m_scheduleTimer;

    QMap<int, RequestContext> m_pendingRequests;
    QMap<int, QMap<int, int>> m_steamCache;
    QMap<int, int> m_popularityCache;
    QMap<int, QString> m_steamErrorCache;
    QMap<int, QString> m_popErrorCache;

    int m_requestCounter = 0;
    bool m_fetching = false;
};