#include "botManager.h"
#include "config.h"
#include <QDateTime>
#include <QTimeZone>
#include <QDebug>

BotManager::BotManager(TelegramClient* tg, SteamApi* steam, QObject* parent)
    : QObject(parent), m_tg(tg), m_steam(steam)
{
    connect(m_tg, &TelegramClient::messageReceived, this, &BotManager::onNewMessage);
    connect(m_steam, &SteamApi::playersDataReady, this, &BotManager::onSteamDataReady);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &BotManager::scheduleTick);
}

void BotManager::start()
{
    if (Config::TG_TOKEN.isEmpty()) {
        qCritical() << "[BotManager] ERROR: TG_BOT_TOKEN is not set in environment variables!";
        return;
    }

    m_tg->startPolling();

    qDebug() << "[BotManager] Started.";
    qDebug() << "[BotManager] Target Channel ID:" << Config::TARGET_CHAT_ID
             << "Target Topic ID:" << Config::TARGET_TOPIC_ID;

    if (Config::TARGET_CHAT_ID != 0) {
        qDebug() << "[BotManager] Scheduled broadcasts ENABLED for channel.";
    } else {
        qDebug() << "[BotManager] Scheduled broadcasts DISABLED (no channel ID configured).";
    }

    scheduleNextRun();
}

void BotManager::onNewMessage(const TgMessage& msg)
{
    QString cmd = msg.text.trimmed().toLower();

    if (cmd == "/playercount" || cmd.startsWith("/playercount@"))
    {
        // Create context for this specific user request
        RequestContext ctx;
        ctx.chatId = msg.chatId;
        ctx.topicId = msg.messageThreadId;

        fetchAndBroadcast(ctx);
    }
    else if (cmd == "/start")
    {
        m_tg->sendMessage(msg.chatId, "🤖 Bot started. Use /playercount to check stats.", msg.messageThreadId);
    }
}

void BotManager::fetchAndBroadcast(const RequestContext& context)
{
    if (m_fetching)
    {
        qDebug() << "[BotManager] Request already in progress, ignoring new one.";
        return;
    }

    m_fetching = true;
    m_requestCounter++;

    // Store context associated with this unique request ID
    m_pendingRequests[m_requestCounter] = context;
    qDebug() << "[BotManager] Created request ID:" << m_requestCounter;

    m_steam->requestCurrentPlayers(Config::STEAM_APP_IDS, m_requestCounter);
}

void BotManager::onSteamDataReady(const QMap<int, int>& data, int requestId)
{
    m_fetching = false;

    // Retrieve the context for this specific request
    if (m_pendingRequests.contains(requestId))
    {
        RequestContext ctx = m_pendingRequests.take(requestId); // take() removes and returns

        if (ctx.chatId != 0)
        {
            QString report = formatReport(data);
            m_tg->sendMessage(ctx.chatId, report, ctx.topicId);
        }
    }
    else
    {
        qWarning() << "[BotManager] Received data for unknown request ID:" << requestId;
    }
}

void BotManager::scheduleTick()
{
    // Only broadcast if channel ID is configured
    if (Config::TARGET_CHAT_ID != 0)
    {
        // Create context for the scheduled broadcast
        RequestContext ctx;
        ctx.chatId = Config::TARGET_CHAT_ID;
        ctx.topicId = Config::TARGET_TOPIC_ID;

        qDebug() << "[BotManager] Scheduled broadcast triggered.";
        fetchAndBroadcast(ctx);
    }
    scheduleNextRun();
}

qint64 BotManager::msecToNextScheduledTime()
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QDateTime now = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    QDateTime target = now;
    target.setTime(QTime(Config::SCHEDULE_HOUR, 0, 0));

    if (now >= target)
    {
        target = target.addDays(1);
    }

    return now.msecsTo(target);
}

void BotManager::scheduleNextRun()
{
    qint64 delay = msecToNextScheduledTime();
    m_scheduleTimer.start(delay);
}

QString BotManager::formatReport(const QMap<int, int>& data)
{
    int d2Count = data.value(Config::DESTINY_ID, -1);
    int marCount = data.value(Config::MARATHON_ID, -1);

    QString d2Str = d2Count >= 0 ? QString("<code>%1</code>").arg(QLocale().toString(d2Count)) : "<code>❌ N/A</code>";
    QString marStr = marCount >= 0 ? QString("<code>%1</code>").arg(QLocale().toString(marCount)) : "<code>❌ N/A</code>";

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc()
                          .toTimeZone(tz)
                          .toString("HH:mm (dd.MM.yyyy)");

    QString d2Link = QString("<a href=\"https://store.steampowered.com/app/%1/\"><b>Destiny 2</b></a>")
                         .arg(Config::DESTINY_ID);
    QString marLink = QString("<a href=\"https://store.steampowered.com/app/%1/\"><b>Marathon</b></a>")
                          .arg(Config::MARATHON_ID);

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";

    return QString("📊 <b>Steam Online Report</b>\n"
                   "%1\n"
                   "🕒 <i>Kyiv Time: %2</i>\n\n"
                   "🎮 %3\n   👥 %4 players\n\n"
                   "🌌 %5\n   👥 %6 players\n\n"
                   "%7\n"
                   "✅ Data fresh • Bot v1.0")
        .arg(separator)
        .arg(timeStr)
        .arg(d2Link)
        .arg(d2Str)
        .arg(marLink)
        .arg(marStr)
        .arg(separator);
}