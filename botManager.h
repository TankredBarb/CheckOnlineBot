#pragma once

#include <QObject>
#include <QTimer>
#include <QMap>
#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"
#include "uptimeTracker.h"

struct RequestContext
{
    qint64 chatId = 0;
    qint64 topicId = 0;

    enum class RequestType
    {
        PlayerCount,
        Uptime
    };

    RequestType type = RequestType::PlayerCount;
};

class BotManager : public QObject
{
    Q_OBJECT
public:
    // Constructor now accepts uptime tracker by const reference
    explicit BotManager(TelegramClient* tg, SteamApi* steam, PopularityApi* popularity, const UptimeTracker& uptime, QObject* parent = nullptr);
    void start();

    // Public accessor for uptime (read-only)
    const UptimeTracker& uptime() const;

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
    void sendUptimeReport(int requestId);

    TelegramClient* m_tg;
    SteamApi* m_steam;
    PopularityApi* m_popularity;
    const UptimeTracker& m_uptime;  // Reference, not owned
    QTimer m_scheduleTimer;

    QMap<int, RequestContext> m_pendingRequests;
    QMap<int, QMap<int, int>> m_steamCache;
    QMap<int, int> m_popularityCache;
    QMap<int, QString> m_steamErrorCache;
    QMap<int, QString> m_popErrorCache;

    int m_requestCounter = 0;
    bool m_fetching = false;

    // Slow mode / rate limit settings
    QSet<qint64> m_processedUpdateIds;
    QTimer m_cleanupTimer;

    QHash<qint64, qint64> m_lastRequestTime;
    static constexpr qint64 RATE_LIMIT_MS = 500;
};