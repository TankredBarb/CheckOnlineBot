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
    m_tg->startPolling();
    scheduleNextRun();
    qDebug() << "[BotManager] Started. Next broadcast at"
             << QDateTime::currentDateTime().addMSecs(msecToNextScheduledTime()).toString("dd.MM.yyyy HH:mm:ss");
}

void BotManager::onNewMessage(const TgMessage& msg)
{
    QString cmd = msg.text.trimmed().toLower();

    // Check for command (supports both /playercount and /playercount@BotName)
    if (cmd == "/playercount" || cmd.startsWith("/playercount@"))
    {
        // Route response to the exact chat and topic where command was received
        m_targetChatId = msg.chatId;
        m_targetTopicId = msg.messageThreadId;

        qDebug() << "[BotManager] Command received. Chat:" << m_targetChatId
                 << "Topic:" << m_targetTopicId;

        fetchAndBroadcast();
    }
    else if (cmd == "/start")
    {
        m_tg->sendMessage(msg.chatId, "Bot started. Use /playercount to check stats.", msg.messageThreadId);
    }
}

void BotManager::fetchAndBroadcast()
{
    if (m_fetching)
    {
        qDebug() << "[BotManager] Request already in progress, ignoring new one.";
        return;
    }
    m_fetching = true;
    m_steam->requestCurrentPlayers(Config::STEAM_APP_IDS);
}

void BotManager::onSteamDataReady(const QMap<int, int>& data)
{
    m_fetching = false;
    QString report = formatReport(data);

    if (m_targetChatId != 0)
    {
        m_tg->sendMessage(m_targetChatId, report, m_targetTopicId);

        // Reset state
        m_targetChatId = 0;
        m_targetTopicId = 0;
    }
    else
    {
        qWarning() << "[BotManager] No target chat ID set for response.";
    }
}

void BotManager::scheduleTick()
{
    // Scheduled broadcast goes strictly to the configured Target Chat/Topic
    if (Config::TARGET_CHAT_ID != 0)
    {
        m_targetChatId = Config::TARGET_CHAT_ID;
        m_targetTopicId = Config::TARGET_TOPIC_ID; // Use the specific release topic ID

        qDebug() << "[BotManager] Scheduled broadcast triggered for Chat:"
                 << m_targetChatId << "Topic:" << m_targetTopicId;

        fetchAndBroadcast();
    }
    else
    {
        qDebug() << "[BotManager] TARGET_CHAT_ID not configured, skipping scheduled broadcast.";
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

    // Format numbers with locale-aware thousand separators, monospace font for alignment
    QString d2Str = d2Count >= 0 ? QString("<code>%1</code>").arg(QLocale().toString(d2Count)) : "<code>❌ N/A</code>";
    QString marStr = marCount >= 0 ? QString("<code>%1</code>").arg(QLocale().toString(marCount)) : "<code>❌ N/A</code>";

    // Resolve Kyiv timezone and format current time
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc()
                          .toTimeZone(tz)
                          .toString("HH:mm (dd.MM.yyyy)");

    // Build clickable Steam store links with bold text
    QString d2Link = QString("<a href=\"https://store.steampowered.com/app/%1/\"><b>Destiny 2</b></a>")
                         .arg(Config::DESTINY_ID);
    QString marLink = QString("<a href=\"https://store.steampowered.com/app/%1/\"><b>Marathon</b></a>")
                          .arg(Config::MARATHON_ID);

    // Visual separator for clean structure
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