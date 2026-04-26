#include "botManager.h"
#include "config.h"
#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <QRegularExpression>

BotManager::BotManager(TelegramClient* tg, SteamApi* steam, PopularityApi* popularity, const UptimeTracker& uptime, QObject* parent)
    : QObject(parent), m_tg(tg), m_steam(steam), m_popularity(popularity), m_uptime(uptime), m_cleanupTimer(this)
{
    connect(m_tg, &TelegramClient::messageReceived, this, &BotManager::onNewMessage);
    connect(m_tg, &TelegramClient::callbackQueryReceived, this, &BotManager::handleCallbackQuery);
    connect(m_steam, &SteamApi::playersDataReady, this, &BotManager::onSteamDataReady);
    connect(m_popularity, &PopularityApi::popularityDataReady, this, &BotManager::onPopularityDataReady);

    // [+] Connect platform distribution signal (with requestId)
    connect(m_popularity, &PopularityApi::platformDistributionReceived,
            this, &BotManager::onPlatformDistributionDataReady);

    connect(&m_scheduleTimer, &QTimer::timeout, this, &BotManager::scheduleTick);

    m_cleanupTimer.setInterval(300000);
    connect(&m_cleanupTimer, &QTimer::timeout, this, [this] {
        m_processedUpdateIds.clear();
        m_lastRequestTime.clear();
        qDebug() << "[BotManager] Cleared duplicate/rate-limit cache";
    });
    m_cleanupTimer.start();
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
    qDebug() << "[BotManager] Target Channel ID:" << Config::TARGET_CHAT_ID;

    if (Config::TARGET_CHAT_ID != 0)
    {
        qDebug() << "[BotManager] Scheduled broadcasts ENABLED.";
    }
    scheduleNextRun();
}

const UptimeTracker& BotManager::uptime() const
{
    return m_uptime;
}

void BotManager::handleCallbackQuery(const QString& callbackQueryId, const QString& callbackData, qint64 chatId, qint64 topicId)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Rate limit check
    if (m_lastRequestTime.contains(chatId))
    {
        qint64 lastTime = m_lastRequestTime[chatId];
        if (now - lastTime < RATE_LIMIT_MS)
        {
            m_tg->answerCallbackQuery(callbackQueryId, "⏳ Подождите немного перед следующим запросом");
            return;
        }
    }
    m_lastRequestTime[chatId] = now;

    QStringList parts = callbackData.split(':');
    if (parts.size() != 2)
    {
        m_tg->answerCallbackQuery(callbackQueryId);
        return;
    }

    QString action = parts[0];
    int requestId = parts[1].toInt();

    // Acknowledge callback immediately to remove loading state
    m_tg->answerCallbackQuery(callbackQueryId);

    if (action == "uptime")
    {
        QString uptimeStr = m_uptime.toString();
        QDateTime start = m_uptime.startTime();
        QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
        QString localStart = start.toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

        QString reply = QString(
            "⏱ <b>ВРЕМЯ РАБОТЫ БОТА</b>\n"
            "━━━━━━━━━━━━━━━━━━━━\n"
            "🕐 Работает: <b>%1</b>\n"
            "📅 Запущен: <b>%2</b> (Киев)"
        ).arg(uptimeStr).arg(localStart);

        m_tg->sendMessage(chatId, reply, topicId);
    }
    else if (action == "platforms")
    {
        // [+] Create new request for platform distribution from button click
        m_requestCounter++;
        RequestContext ctx;
        ctx.chatId = chatId;
        ctx.topicId = topicId;
        ctx.type = RequestContext::RequestType::PlatformDistribution;
        m_pendingRequests[m_requestCounter] = ctx;

        qDebug() << "[BotManager] Platform request from button. ID:" << m_requestCounter
                 << "Chat:" << chatId << "Topic:" << topicId;

        // Call API with new requestId
        m_popularity->requestPlatformDistribution(m_requestCounter);

        // Cleanup after 5 minutes
        QTimer::singleShot(300000, this, [this, reqId = m_requestCounter]() {
            m_pendingRequests.remove(reqId);
            qDebug() << "[BotManager] Cleaned up platform button request" << reqId;
        });
    }
    else
    {
        // Silent acknowledge for unknown actions
    }
}

void BotManager::onNewMessage(const TgMessage& msg)
{
    QString cmd = msg.text.trimmed().toLower();

    bool isBotCommand = cmd.startsWith("/") &&
        (cmd == "/playercount" || cmd.startsWith("/playercount@") ||
         cmd == "/uptime" || cmd.startsWith("/uptime@") ||
         cmd == "/platforms" || cmd.startsWith("/platforms@") ||
         cmd == "/start");

    if (isBotCommand)
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastRequestTime.contains(msg.chatId))
        {
            qint64 lastTime = m_lastRequestTime[msg.chatId];
            if (now - lastTime < RATE_LIMIT_MS)
            {
                qDebug() << "[BotManager] RATE LIMITED. ChatID:" << msg.chatId
                         << "Wait" << (RATE_LIMIT_MS - (now - lastTime)) << "ms";
                return;
            }
        }
        m_lastRequestTime[msg.chatId] = now;

        qDebug() << "[BotManager] Command received. ChatID:" << msg.chatId << "Text:" << msg.text;
    }
    else
    {
        return;
    }

    if (cmd == "/playercount" || cmd.startsWith("/playercount@"))
    {
        RequestContext ctx;
        ctx.chatId = msg.chatId;
        ctx.topicId = msg.messageThreadId;
        ctx.type = RequestContext::RequestType::PlayerCount;
        fetchAndBroadcast(ctx);
    }
    else if (cmd == "/uptime" || cmd.startsWith("/uptime@"))
    {
        RequestContext ctx;
        ctx.chatId = msg.chatId;
        ctx.topicId = msg.messageThreadId;
        ctx.type = RequestContext::RequestType::Uptime;
        fetchAndBroadcast(ctx);
    }
    else if (cmd == "/platforms" || cmd.startsWith("/platforms@"))
    {
        RequestContext ctx;
        ctx.chatId = msg.chatId;
        ctx.topicId = msg.messageThreadId;
        ctx.type = RequestContext::RequestType::PlatformDistribution;
        fetchAndBroadcast(ctx);
    }
    else if (cmd == "/start")
    {
        m_tg->sendMessage(msg.chatId,
            "🤖 <b>Steam Destiny Stats Bot</b>\n\n"
            "Команды:\n"
            "/playercount — Полный отчет по онлайну\n"
            "/platforms — Распределение игроков по платформам\n"
            "/uptime — Время работы бота",
            msg.messageThreadId);
    }
}

void BotManager::fetchAndBroadcast(const RequestContext& context)
{
    if (m_fetching)
    {
        qDebug() << "[BotManager] Request in progress, ignoring new one.";
        return;
    }

    m_fetching = true;
    m_requestCounter++;
    m_pendingRequests[m_requestCounter] = context;

    qDebug() << "[BotManager] Created request ID:" << m_requestCounter
             << "Type:" << static_cast<int>(context.type);

    // [+] Handle PlatformDistribution
    if (context.type == RequestContext::RequestType::PlatformDistribution)
    {
        // Pass requestId so we can track it through the async signal
        m_popularity->requestPlatformDistribution(m_requestCounter);
        return;
    }

    if (context.type == RequestContext::RequestType::Uptime)
    {
        sendUptimeReport(m_requestCounter);
        return;
    }

    // PlayerCount logic (original)
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

void BotManager::sendUptimeReport(int requestId)
{
    m_fetching = false;

    if (!m_pendingRequests.contains(requestId))
    {
        qWarning() << "[BotManager] Uptime request" << requestId << "not found in pending requests";
        return;
    }

    RequestContext ctx = m_pendingRequests[requestId];

    if (ctx.chatId != 0)
    {
        QString uptimeStr = m_uptime.toString();
        QDateTime start = m_uptime.startTime();
        QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
        QString localStart = start.toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

        QString reply = QString(
            "⏱ <b>ВРЕМЯ РАБОТЫ БОТА</b>\n"
            "━━━━━━━━━━━━━━━━━━━━\n"
            "🕐 Работает: <b>%1</b>\n"
            "📅 Запущен: <b>%2</b> (Киев)"
        ).arg(uptimeStr).arg(localStart);

        m_tg->sendMessage(ctx.chatId, reply, ctx.topicId);
        qDebug() << "[BotManager] Uptime sent for request" << requestId;

        QTimer::singleShot(300000, this, [this, requestId]() {
            m_pendingRequests.remove(requestId);
            qDebug() << "[BotManager] Cleaned up uptime request" << requestId;
        });
    }
    else
    {
        qWarning() << "[BotManager] Invalid chatId for uptime request" << requestId;
    }
}

// [+] Slot with requestId parameter
void BotManager::onPlatformDistributionDataReady(const QMap<PlatformCategory, int>& platformStats, int requestId)
{
    m_fetching = false;

    // Now we can reliably find the context because requestId was passed through
    if (!m_pendingRequests.contains(requestId))
    {
        qDebug() << "[BotManager] Platform request" << requestId << "not found (expired?)";
        return;
    }

    sendPlatformReport(requestId, platformStats);
}

void BotManager::onSteamDataReady(const QMap<int, int>& data, const QString& error, int requestId)
{
    if (m_pendingRequests.contains(requestId) &&
        m_pendingRequests[requestId].type == RequestContext::RequestType::PlayerCount)
    {
        m_steamCache[requestId] = data;
        m_steamErrorCache[requestId] = error;
        checkAndSend(requestId);
    }
    else
    {
        qDebug() << "[BotManager] Ignoring Steam data for non-PlayerCount request" << requestId;
    }
}

void BotManager::onPopularityDataReady(int players, const QString& error, QString gameSlug, int requestId)
{
    if (m_pendingRequests.contains(requestId) &&
        m_pendingRequests[requestId].type == RequestContext::RequestType::PlayerCount)
    {
        m_popularityCache[requestId] = players;
        m_popErrorCache[requestId] = error;
        checkAndSend(requestId);
    }
    else
    {
        qDebug() << "[BotManager] Ignoring Popularity data for non-PlayerCount request" << requestId;
    }
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

QJsonObject BotManager::buildInlineKeyboard(int requestId)
{
    QJsonArray keyboardRows;

    {
        QJsonArray row;
        QJsonObject btn;
        btn["text"] = "⏱ Показать аптайм";
        btn["callback_data"] = QString("uptime:%1").arg(requestId);
        row.append(btn);
        keyboardRows.append(row);
    }

    {
        QJsonArray row;
        QJsonObject btn;
        btn["text"] = "📊 Платформы";  // [+] Updated text
        btn["callback_data"] = QString("platforms:%1").arg(requestId);
        row.append(btn);
        keyboardRows.append(row);
    }

    QJsonObject replyMarkup;
    replyMarkup["inline_keyboard"] = keyboardRows;
    return replyMarkup;
}

void BotManager::sendReport(int requestId)
{
    m_fetching = false;

    if (!m_pendingRequests.contains(requestId))
        return;

    RequestContext ctx = m_pendingRequests[requestId];
    auto steamData = m_steamCache.take(requestId);
    int popData = m_popularityCache.value(requestId, -1);
    m_popularityCache.remove(requestId);

    QString steamErr = m_steamErrorCache.value(requestId, "");
    m_steamErrorCache.remove(requestId);

    QString popErr = m_popErrorCache.value(requestId, "");
    m_popErrorCache.remove(requestId);

    if (ctx.chatId != 0)
    {
        QString report = formatReport(steamData, steamErr, popData, popErr);
        QJsonObject replyMarkup = buildInlineKeyboard(requestId);

        m_tg->sendMessage(ctx.chatId, report, ctx.topicId, replyMarkup);

        qDebug() << "[BotManager] Report sent for request" << requestId;

        QTimer::singleShot(300000, this, [this, requestId]() {
            m_pendingRequests.remove(requestId);
            qDebug() << "[BotManager] Cleaned up request" << requestId;
        });
    }
}

QString BotManager::formatPlatformReport(const QMap<PlatformCategory, int>& platformStats)
{
    int totalPlayers = 0;
    for (int count : platformStats) totalPlayers += count;

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    QString report = QString(
        "📊 <b>РАСПРЕДЕЛЕНИЕ ПО ПЛАТФОРМАМ</b>\n"
        "%1\n"
        "🕒 %2\n\n"
    ).arg("━━━━━━━━━━━━━━━━━━━━").arg(timeStr);

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore,
        PlatformCategory::Stadia
    };

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat)) continue;

        int players = platformStats[cat];
        if (players <= 0) continue;

        double percent = totalPlayers > 0 ? (players * 100.0 / totalPlayers) : 0;
        QString icon;

        switch (cat)
        {
            case PlatformCategory::PlayStation: icon = "🎮"; break;
            case PlatformCategory::Xbox: icon = "💚"; break;
            case PlatformCategory::Steam: icon = "⚙️"; break;
            case PlatformCategory::EpicGamesStore: icon = "🦅"; break;
            case PlatformCategory::Stadia: icon = "☠️"; break;
            default: icon = "•"; break;
        }

        // Format number with spaces (e.g. 1 000)
        QString formattedPlayers = QString::number(players).replace(QRegularExpression("(\\d)(?=(\\d{3})+(?!\\d))"), "\\1 ");

        report += QString("%1 <b>%2</b>: %3 (%4%)\n")
                      .arg(icon)
                      .arg(platformCategoryToString(cat))
                      .arg(formattedPlayers)
                      .arg(QString::number(percent, 'f', 1));
    }

    report += QString("\n%1\n<i>📊 Данные из Popularity.report</i>").arg("━━━━━━━━━━━━━━━━━━━━");
    return report;
}

void BotManager::sendPlatformReport(int requestId, const QMap<PlatformCategory, int>& platformStats)
{
    if (!m_pendingRequests.contains(requestId))
        return;

    RequestContext ctx = m_pendingRequests[requestId];

    if (ctx.chatId != 0)
    {
        QString report = formatPlatformReport(platformStats);

        // [+] NO BUTTONS for platform report (empty replyMarkup)
        m_tg->sendMessage(ctx.chatId, report, ctx.topicId);

        qDebug() << "[BotManager] Platform report sent for request" << requestId;

        QTimer::singleShot(300000, this, [this, requestId]() {
            m_pendingRequests.remove(requestId);
            qDebug() << "[BotManager] Cleaned up platform request" << requestId;
        });
    }

    m_fetching = false;
}

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

    QDateTime target = now;
    target.setTime(QTime(Config::SCHEDULE_HOUR, Config::SCHEDULE_MINUTES));
    qDebug() << target;

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

QString BotManager::formatReport(const QMap<int, int>& steamData, const QString& steamError,
                                 int destinyAllPlatforms, const QString& popError)
{
    auto fmt = [](int val) -> QString { return val >= 0 ? QString::number(val) : "N/A"; };

    const QString d2Link = "https://store.steampowered.com/app/1085660";
    const QString marLink = "https://store.steampowered.com/app/3065800";

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    QString d2Section;
    if (!steamError.isEmpty() && !popError.isEmpty())
    {
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "🔴 <b>Данные временно недоступны</b>\n"
            " <i>Steam API: %2</i>\n"
            " <i>Global API: %3</i>\n"
            " <i>Попробуйте повторить запрос через несколько минут.</i>"
        ).arg(d2Link).arg(steamError).arg(popError);
    }
    else if (!steamError.isEmpty())
    {
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "🔴 <b>Steam API недоступен:</b> %2\n"
            " <i>Пробуем показать глобальные данные, если они доступны.</i>"
        ).arg(d2Link).arg(steamError);

        if (destinyAllPlatforms >= 0 && popError.isEmpty())
        {
            d2Section += QString("\n🌍 <b>Все платформы: ~%1</b> игроков\n <i>⚠️ Примерные данные за 24ч</i>").arg(fmt(destinyAllPlatforms));
        }
    }
    else if (!popError.isEmpty())
    {
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "├─ 💻 Steam: <b>%2</b> игроков сейчас\n"
            "🔴 <b>Global API недоступен:</b> %3"
        ).arg(d2Link).arg(fmt(steamData.value(Config::DESTINY_ID, -1))).arg(popError);
    }
    else
    {
        d2Section = QString(
            "🎮 <b>Destiny 2</b> — <a href=\"%1\">Steam Store</a>\n"
            "├─ 💻 Steam: <b>%2</b> игроков сейчас\n"
            "└─  <b>Все платформы: ~%3</b> игроков\n"
            " <i>⚠️ Примерные данные за последние 24ч</i>\n"
            " <i>(включая PlayStation, Xbox, PC)</i>"
        ).arg(d2Link).arg(fmt(steamData.value(Config::DESTINY_ID, -1))).arg(fmt(destinyAllPlatforms));
    }

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

    QString disclaimer;
    if (!steamError.isEmpty() || !popError.isEmpty())
    {
        disclaimer = "━━━━━━━━━━━━━━━━━━━━\n"
            " <i>⚠️ Обнаружены проблемы с внешними API. Данные могут быть неполными.</i>\n"
            " <i>📊 Глобальная статистика из Popularity.report — примерная оценка за 24ч.</i>\n"
            " <i>Используйте с осторожностью, реальный онлайн может отличаться.</i>";
    }
    else
    {
        disclaimer = "━━━━━━━━━━━━━━━━━━━━\n"
            " <i>📊 Глобальная статистика из Popularity.report — примерная оценка за 24ч.</i>\n"
            " <i>⚠️ Используйте с осторожностью, реальный онлайн может отличаться.</i>";
    }

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";

    return QString(
        "📊 <b>ОНЛАЙН В ИГРАХ</b>\n"
        "<a href=\"%1\">🔗 bungie.net</a>\n"
        "%2\n"
        "🕒 %3\n\n"
        "%4\n\n"
        "%5\n\n"
        "%6\n"
        "%7"
    ).arg(Config::BUNGIE_PREVIEW_URL).arg(separator).arg(timeStr).arg(d2Section).arg(marSection).arg(separator).arg(disclaimer);
}