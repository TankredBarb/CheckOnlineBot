#pragma once

// ==================== INCLUDES ====================
#include <QObject>
#include <QMap>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <QRegularExpression>

// [+] Conditional GUI includes
#ifdef USE_GUI_CHARTS
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#endif

#include "telegramClient.h"
#include "steamApi.h"
#include "popularityApi.h"
#include "uptimeTracker.h"

// ==================== DATA STRUCTURES ====================

struct RequestContext
{
    qint64 chatId = 0;
    qint64 topicId = 0;

    enum class RequestType
    {
        PlayerCount,
        ShortStats,
        Uptime,
        PlatformDistribution
    };

    RequestType type = RequestType::PlayerCount;
};

// ==================== CLASS DECLARATION ====================

class BotManager : public QObject
{
    Q_OBJECT

public:
    // --- Constructor & Lifecycle ---
    explicit BotManager(TelegramClient* tg, SteamApi* steam, PopularityApi* popularity, const UptimeTracker& uptime, QObject* parent = nullptr);
    void start();

    // --- Public Interface ---
    const UptimeTracker& uptime() const;

private slots:
    // --- Event Handlers: Input ---
    void onNewMessage(const TgMessage& msg);
    void handleCallbackQuery(const QString& callbackQueryId, const QString& callbackData, qint64 chatId, qint64 topicId);

    // --- Event Handlers: API Callbacks ---
    void onSteamDataReady(const QMap<int, int>& data, const QString& error, int requestId);
    void onPopularityDataReady(int players, const QString& error, QString gameSlug, int requestId);
    void onPlatformDistributionDataReady(const QMap<PlatformCategory, int>& platformStats, int requestId);

    // --- Event Handlers: Scheduler ---
    void scheduleTick();

private:
    // --- Request Routing ---
    void fetchAndBroadcast(const RequestContext& context);
    void checkAndSend(int requestId);

    // --- Response Generators: Player Count ---
    void sendReport(int requestId);
    QString formatReport(const QMap<int, int>& steamData, const QString& steamError,
                         int destinyAllPlatforms, const QString& popError);
    QJsonObject buildInlineKeyboard(int requestId);

    // --- Response Generators: Short Stats ---
    void sendShortReport(int requestId);
    QString formatShortReport(const QMap<int, int>& steamData, const QString& steamError,
                              int destinyAllPlatforms, const QString& popError);

    // --- Response Generators: Uptime ---
    void sendUptimeReport(int requestId);

    // --- Response Generators: Platforms ---
    void sendPlatformReport(int requestId, const QMap<PlatformCategory, int>& platformStats);
    QString formatPlatformReport(const QMap<PlatformCategory, int>& platformStats);

    // [+] Conditional GUI methods
#ifdef USE_GUI_CHARTS
    QImage generatePlatformChart(const QMap<PlatformCategory, int>& platformStats, int totalPlayers);
#endif

    QString generateTextPlatformReport(const QMap<PlatformCategory, int>& platformStats, int totalPlayers);
    QString generateCompactPlatformReport(const QMap<PlatformCategory, int>& platformStats, int totalPlayers);

    // --- Scheduler Helpers ---
    void scheduleNextRun();
    qint64 msecToNextScheduledTime();

    // --- Dependencies ---
    TelegramClient* m_tg;
    SteamApi* m_steam;
    PopularityApi* m_popularity;
    const UptimeTracker& m_uptime;

    // --- Timers ---
    QTimer m_scheduleTimer;
    QTimer m_cleanupTimer;

    // --- Request State ---
    QMap<int, RequestContext> m_pendingRequests;
    int m_requestCounter = 0;
    bool m_fetching = false;

    // --- Caches ---
    QMap<int, QMap<int, int>> m_steamCache;
    QMap<int, int> m_popularityCache;
    QMap<int, QString> m_steamErrorCache;
    QMap<int, QString> m_popErrorCache;

    // --- Rate Limiting & Cleanup ---
    QSet<int> m_processedUpdateIds;
    QHash<qint64, qint64> m_lastRequestTime;
    static constexpr qint64 RATE_LIMIT_MS = 500;
};