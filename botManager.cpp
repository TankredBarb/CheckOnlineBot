#include "botManager.h"
#include "config.h"
#include <QDebug>
#include <QDateTime>
#include <QTimeZone>
#include <QLocale>

BotManager::BotManager(TelegramClient* tg, SteamApi* steam, PopularityApi* popularity, QObject* parent)
    : QObject(parent), m_tg(tg), m_steam(steam), m_popularity(popularity)
{
    connect(m_tg, &TelegramClient::messageReceived, this, &BotManager::onNewMessage);
    connect(m_steam, &SteamApi::playersDataReady, this, &BotManager::onSteamDataReady);
    connect(m_popularity, &PopularityApi::popularityDataReady, this, &BotManager::onPopularityDataReady);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &BotManager::scheduleTick);
}

void BotManager::start()
{
    if (Config::TG_TOKEN.isEmpty())
    {
        qCritical() << "[BotManager] ERROR: TG_BOT_TOKEN is not set!";
        return;
    }

    m_tg->startPolling();
    qDebug() << "[BotManager] Started.";
    qDebug() << "[BotManager] Target Channel ID:" << Config::TARGET_CHAT_ID
             << "Target Topic ID:" << Config::TARGET_TOPIC_ID;

    if (Config::TARGET_CHAT_ID != 0)
    {
        qDebug() << "[BotManager] Scheduled broadcasts ENABLED.";
    }
    scheduleNextRun();
}

void BotManager::onNewMessage(const TgMessage& msg)
{
    QString cmd = msg.text.trimmed().toLower();

    if (cmd == "/playercount" || cmd.startsWith("/playercount@"))
    {
        RequestContext ctx{msg.chatId, msg.messageThreadId};
        fetchAndBroadcast(ctx);
    }
    else if (cmd == "/start")
    {
        m_tg->sendMessage(msg.chatId, "🤖 Бот готов. Используйте /playercount для проверки статистики.", msg.messageThreadId);
    }
}

void BotManager::fetchAndBroadcast(const RequestContext& context)
{
    if (m_fetching)
    {
        qDebug() << "[BotManager] Request in progress, ignoring.";
        return;
    }

    m_fetching = true;
    m_requestCounter++;
    m_pendingRequests[m_requestCounter] = context;

    // Clear caches for new request
    m_steamCache.remove(m_requestCounter);
    m_popularityCache.remove(m_requestCounter);
    m_steamErrorCache.remove(m_requestCounter);
    m_popErrorCache.remove(m_requestCounter);

    qDebug() << "[BotManager] Fetching data for request ID:" << m_requestCounter;
    m_steam->requestCurrentPlayers(Config::STEAM_APP_IDS, m_requestCounter);

    if (!Config::POPULARITY_API_KEY.isEmpty())
    {
        m_popularity->requestCrossPlatformPlayer(Config::DESTINY_SLUG, m_requestCounter);
    }
    else
    {
        onPopularityDataReady(-1, "", Config::DESTINY_SLUG, m_requestCounter);
    }
}

// --- SYNCHRONIZATION OF TWO APIS ---
void BotManager::onSteamDataReady(const QMap<int, int>& data, const QString& error, int requestId)
{
    m_steamCache[requestId] = data;
    m_steamErrorCache[requestId] = error;
    checkAndSend(requestId);
}

void BotManager::onPopularityDataReady(int players, const QString& error, QString gameSlug, int requestId)
{
    m_popularityCache[requestId] = players;
    m_popErrorCache[requestId] = error;
    checkAndSend(requestId);
}

void BotManager::checkAndSend(int requestId)
{
    if (!m_pendingRequests.contains(requestId))
        return;

    bool steamReady = m_steamCache.contains(requestId);
    bool popReady = m_popularityCache.contains(requestId) || Config::POPULARITY_API_KEY.isEmpty();

    if (steamReady && popReady)
    {
        sendReport(requestId);
    }
}

void BotManager::sendReport(int requestId)
{
    m_fetching = false;

    if (!m_pendingRequests.contains(requestId))
        return;

    RequestContext ctx = m_pendingRequests.take(requestId);
    auto steamData = m_steamCache.take(requestId);
    int popData = m_popularityCache.value(requestId, -1);
    m_popularityCache.remove(requestId);

    // ИСПРАВЛЕНО: используем value() + remove() или просто take() без 2-го аргумента
    QString steamErr = m_steamErrorCache.value(requestId, "");
    m_steamErrorCache.remove(requestId);

    QString popErr = m_popErrorCache.value(requestId, "");
    m_popErrorCache.remove(requestId);

    if (ctx.chatId != 0)
    {
        QString report = formatReport(steamData, steamErr, popData, popErr);
        m_tg->sendMessage(ctx.chatId, report, ctx.topicId);
        qDebug() << "[BotManager] Report sent for request" << requestId;
    }
}
// --- SCHEDULING ---
void BotManager::scheduleTick()
{
    if (Config::TARGET_CHAT_ID != 0)
    {
        RequestContext ctx;
        ctx.chatId = Config::TARGET_CHAT_ID;
        ctx.topicId = 0;

        qDebug() << "[BotManager] Scheduled broadcast triggered.";
        fetchAndBroadcast(ctx);
    }
    scheduleNextRun();
}

qint64 BotManager::msecToNextScheduledTime()
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QDateTime now = QDateTime::currentDateTimeUtc().toTimeZone(tz);

    // Build target datetime for today with configured HH:MM
    QDateTime target = now;
    target.setTime(Config::SCHEDULE_TIME);
    qDebug() << target;

    // If target time already passed today, schedule for tomorrow
    if (now >= target)
    {
        target = target.addDays(1);
    }

        return now.msecsTo(target);
}

void BotManager::scheduleNextRun()
{
    m_scheduleTimer.start(msecToNextScheduledTime());
}


// --- FORMATTING WITH ERROR HANDLING ---
QString BotManager::formatReport(const QMap<int, int>& steamData, const QString& steamError,
                                 int destinyAllPlatforms, const QString& popError)
{
    auto fmt = [](int val) -> QString { return val >= 0 ? QString::number(val) : "N/A"; };

    const QString d2Link = "https://store.steampowered.com/app/1085660";
    const QString marLink = "https://store.steampowered.com/app/3065800";

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    // --- Destiny 2 Section ---
    QString d2Section;
    if (!steamError.isEmpty() && !popError.isEmpty())
    {
        // Both sources failed
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "🔴 <b>Данные временно недоступны</b>\n"
            "   <i>Steam API: %2</i>\n"
            "   <i>Global API: %3</i>\n"
            "   <i>Попробуйте повторить запрос через несколько минут.</i>"
        ).arg(d2Link).arg(steamError).arg(popError);
    }
    else if (!steamError.isEmpty())
    {
        // Only Steam failed
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "🔴 <b>Steam API недоступен:</b> %2\n"
            "   <i>Пробуем показать глобальные данные, если они доступны.</i>"
        ).arg(d2Link).arg(steamError);

        if (destinyAllPlatforms >= 0 && popError.isEmpty())
        {
            d2Section += QString("\n🌍 <b>Все платформы: ~%1</b> игроков\n<i>⚠️ Примерные данные за 24ч</i>").arg(fmt(destinyAllPlatforms));
        }
    }
    else if (!popError.isEmpty())
    {
        // Only Global failed
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "├─ 💻 Steam: <b>%2</b> игроков сейчас\n"
            "🔴 <b>Global API недоступен:</b> %3"
        ).arg(d2Link).arg(fmt(steamData.value(Config::DESTINY_ID, -1))).arg(popError);
    }
    else
    {
        // All OK
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "├─ 💻 Steam: <b>%2</b> игроков сейчас\n"
            "└─ 🌍 <b>Все платформы: ~%3</b> игроков\n"
            "   <i>⚠️ Примерные данные за последние 24ч</i>\n"
            "   <i>(включая PlayStation, Xbox, PC)</i>"
        ).arg(d2Link).arg(fmt(steamData.value(Config::DESTINY_ID, -1))).arg(fmt(destinyAllPlatforms));
    }

    // --- Marathon Section ---
    QString marSection;
    if (!steamError.isEmpty())
    {
        marSection = QString(
            "🌌 <b>Marathon</b> — <a href=\"%1\">Steam Store</a>\n"
            "🔴 <b>Steam API недоступен:</b> %2"
        ).arg(marLink).arg(steamError);
    }
    else
    {
        marSection = QString(
            "🌌 <b>Marathon</b> — <a href=\"%1\">Steam Store</a>\n"
            "└─ 💻 Steam: <b>%2</b> игроков сейчас"
        ).arg(marLink).arg(fmt(steamData.value(Config::MARATHON_ID, -1)));
    }

    // --- Footer / Disclaimer ---
    QString disclaimer;
    if (!steamError.isEmpty() || !popError.isEmpty())
    {
        disclaimer = "━━━━━━━━━━━━━━━━━━━━\n"
                     "<i>⚠️ Обнаружены проблемы с внешними API. Данные могут быть неполными.</i>\n"
                     "<i>📊 Глобальная статистика из Popularity.report — примерная оценка за 24ч.</i>\n"
                     "<i>Используйте с осторожностью, реальный онлайн может отличаться.</i>";
    }
    else
    {
        disclaimer = "━━━━━━━━━━━━━━━━━━━━\n"
                     "<i>📊 Глобальная статистика из Popularity.report — примерная оценка за 24ч.</i>\n"
                     "<i>⚠️ Используйте с осторожностью, реальный онлайн может отличаться.</i>";
    }

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";

    return QString(
        "📊 <b>ОНЛАЙН В ИГРАХ</b>\n"
        "%1\n"
        "🕒 %2\n\n"
        "%3\n\n"
        "%4\n\n"
        "%5\n"
        "%6"
    ).arg(separator).arg(timeStr).arg(d2Section).arg(marSection).arg(separator).arg(disclaimer);
}