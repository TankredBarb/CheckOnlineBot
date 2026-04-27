#include "botManager.h"
#include "config.h"
#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <QRegularExpression>

// [+] Conditional GUI includes
#ifdef USE_GUI_CHARTS
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#endif

// ==================== CONSTRUCTOR & LIFECYCLE ====================

BotManager::BotManager(TelegramClient* tg, SteamApi* steam, PopularityApi* popularity, const UptimeTracker& uptime, QObject* parent)
    : QObject(parent), m_tg(tg), m_steam(steam), m_popularity(popularity), m_uptime(uptime), m_cleanupTimer(this)
{
    connect(m_tg, &TelegramClient::messageReceived, this, &BotManager::onNewMessage);
    connect(m_tg, &TelegramClient::callbackQueryReceived, this, &BotManager::handleCallbackQuery);
    connect(m_steam, &SteamApi::playersDataReady, this, &BotManager::onSteamDataReady);
    connect(m_popularity, &PopularityApi::popularityDataReady, this, &BotManager::onPopularityDataReady);

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

    // [+] FIXED: Log both TARGET_CHAT_ID and TARGET_TOPIC_ID for user commands
    qDebug() << "[BotManager] Target Chat ID:" << Config::TARGET_CHAT_ID
             << "Topic ID:" << Config::TARGET_TOPIC_ID;

    if (Config::TARGET_CHAT_ID != 0)
    {
        qDebug() << "[BotManager] Scheduled broadcasts ENABLED (to General topic).";
    }
    scheduleNextRun();
}

const UptimeTracker& BotManager::uptime() const
{
    return m_uptime;
}

// ==================== INPUT HANDLERS ====================

void BotManager::onNewMessage(const TgMessage& msg)
{
    QString cmd = msg.text.trimmed().toLower();

    bool isBotCommand = cmd.startsWith("/") &&
        (cmd == "/playercount" || cmd.startsWith("/playercount@") ||
         cmd == "/short" || cmd.startsWith("/short@") ||
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
    else if (cmd == "/short" || cmd.startsWith("/short@"))
    {
        RequestContext ctx;
        ctx.chatId = msg.chatId;
        ctx.topicId = msg.messageThreadId;
        ctx.type = RequestContext::RequestType::ShortStats;
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
            "/short — Краткий отчет по онлайну\n"
            "/platforms — Распределение игроков по платформам\n"
            "/uptime — Время работы бота",
            msg.messageThreadId);
    }
}

void BotManager::handleCallbackQuery(const QString& callbackQueryId, const QString& callbackData, qint64 chatId, qint64 topicId)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

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
    // requestId from button is just for reference, we create new one for the action
    // int refRequestId = parts[1].toInt();

    m_tg->answerCallbackQuery(callbackQueryId);

    if (action == "uptime")
    {
        // Use the same requestId system as commands for consistency
        m_requestCounter++;
        RequestContext ctx;
        ctx.chatId = chatId;
        ctx.topicId = topicId;
        ctx.type = RequestContext::RequestType::Uptime;
        m_pendingRequests[m_requestCounter] = ctx;

        qDebug() << "[BotManager] Uptime request from button. ID:" << m_requestCounter
                 << "Chat:" << chatId << "Topic:" << topicId;

        sendUptimeReport(m_requestCounter);

        // Cleanup after 5 minutes
        QTimer::singleShot(300000, this, [this, reqId = m_requestCounter]() {
            m_pendingRequests.remove(reqId);
            qDebug() << "[BotManager] Cleaned up uptime button request" << reqId;
        });
    }
    else if (action == "platforms")
    {
        m_requestCounter++;
        RequestContext ctx;
        ctx.chatId = chatId;
        ctx.topicId = topicId;
        ctx.type = RequestContext::RequestType::PlatformDistribution;
        m_pendingRequests[m_requestCounter] = ctx;

        qDebug() << "[BotManager] Platform request from button. ID:" << m_requestCounter
                 << "Chat:" << chatId << "Topic:" << topicId;

        m_popularity->requestPlatformDistribution(m_requestCounter);

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

// ==================== REQUEST ROUTING ====================

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

    if (context.type == RequestContext::RequestType::PlatformDistribution)
    {
        m_popularity->requestPlatformDistribution(m_requestCounter);
        return;
    }

    if (context.type == RequestContext::RequestType::Uptime)
    {
        sendUptimeReport(m_requestCounter);
        return;
    }

    m_steamCache.remove(m_requestCounter);
    m_popularityCache.remove(m_requestCounter);
    m_steamErrorCache.remove(m_requestCounter);
    m_popErrorCache.remove(m_requestCounter);

    qDebug() << "[BotManager] Fetching data for request ID:" << m_requestCounter;
    m_steam->requestCurrentPlayers(Config::STEAM_APP_IDS, m_requestCounter);

    // Для ShortStats не нужны данные Popularity API
    if (context.type != RequestContext::RequestType::ShortStats)
    {
        if (!Config::POPULARITY_API_KEY.isEmpty())
        {
            m_popularity->requestCrossPlatformPlayer(Config::DESTINY_SLUG, m_requestCounter);
        }
        else
        {
            onPopularityDataReady(-1, "", Config::DESTINY_SLUG, m_requestCounter);
        }
    }
}

void BotManager::checkAndSend(int requestId)
{
    if (!m_pendingRequests.contains(requestId))
        return;

    auto ctx = m_pendingRequests[requestId];
    bool steamReady = m_steamCache.contains(requestId);

    // Для ShortStats нужны только Steam данные
    if (ctx.type == RequestContext::RequestType::ShortStats)
    {
        if (steamReady)
        {
            sendShortReport(requestId);
        }
        return;
    }

    // Для PlayerCount нужны и Steam, и Popularity (если ключ задан)
    bool popReady = m_popularityCache.contains(requestId) || Config::POPULARITY_API_KEY.isEmpty();

    if (steamReady && popReady)
    {
        sendReport(requestId);
    }
}

// ==================== API CALLBACKS ====================

void BotManager::onSteamDataReady(const QMap<int, int>& data, const QString& error, int requestId)
{
    if (m_pendingRequests.contains(requestId) &&
        (m_pendingRequests[requestId].type == RequestContext::RequestType::PlayerCount ||
         m_pendingRequests[requestId].type == RequestContext::RequestType::ShortStats))
    {
        m_steamCache[requestId] = data;
        m_steamErrorCache[requestId] = error;
        checkAndSend(requestId);
    }
    else
    {
        qDebug() << "[BotManager] Ignoring Steam data for request" << requestId
                 << "(type:" << (m_pendingRequests.contains(requestId)
                                 ? static_cast<int>(m_pendingRequests[requestId].type)
                                 : -1) << ")";
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

void BotManager::onPlatformDistributionDataReady(const QMap<PlatformCategory, int>& platformStats, int requestId)
{
    m_fetching = false;

    if (!m_pendingRequests.contains(requestId))
    {
        qDebug() << "[BotManager] Platform request" << requestId << "not found (expired?)";
        return;
    }

    sendPlatformReport(requestId, platformStats);
}

// ==================== RESPONSE GENERATORS ====================

// --- Uptime ---
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

// --- Player Count ---
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

// --- Short Stats ---
void BotManager::sendShortReport(int requestId)
{
    m_fetching = false;

    if (!m_pendingRequests.contains(requestId))
        return;

    RequestContext ctx = m_pendingRequests.take(requestId);
    auto steamData = m_steamCache.take(requestId);
    QString steamErr = m_steamErrorCache.take(requestId);

    if (ctx.chatId != 0)
    {
        QString report = formatShortReport(steamData, steamErr, -1, QString());
        m_tg->sendMessage(ctx.chatId, report, ctx.topicId);

        qDebug() << "[BotManager] Short report sent for request" << requestId;
    }
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

    QString buttonNote = "\n\n<i>⏱ Кнопки под сообщением активны 5 минут</i>";

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";

    return QString(
                "📊 <b>ОНЛАЙН В ИГРАХ</b>\n"
                "<a href=\"%1\">🔗 bungie.net</a>\n"
                "%2\n"
                "🕒 %3\n\n"
                "%4\n\n"
                "%5\n\n"
                "%6\n"
                "%7%8"
                ).arg(Config::BUNGIE_PREVIEW_URL)
                 .arg(separator)
                 .arg(timeStr)
                 .arg(d2Section)
                 .arg(marSection)
                 .arg(separator)
                 .arg(disclaimer)
                 .arg(buttonNote);
}

QString BotManager::formatShortReport(const QMap<int, int>& steamData, const QString& steamError,
                                      int destinyAllPlatforms, const QString& popError)
{
    auto fmtOnline = [](int val) -> QString {
        if (val < 0) return "N/A";
        if (val >= 1000) {
            return QString::number(val / 1000.0, 'f', 1) + "k";
        }
        return QString::number(val);
    };

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    // D2
    QString d2Online = "N/A";
    if (steamError.isEmpty() && !popError.isEmpty())
    {
        // Только Steam
        d2Online = fmtOnline(steamData.value(Config::DESTINY_ID, -1));
    }
    else if (!steamError.isEmpty() && popError.isEmpty())
    {
        // Только Popularity
        d2Online = fmtOnline(destinyAllPlatforms);
    }
    else if (steamError.isEmpty() && popError.isEmpty())
    {
        // Оба доступны - показываем Steam как основной
        d2Online = fmtOnline(steamData.value(Config::DESTINY_ID, -1));
    }

    // Marathon
    QString marOnline = fmtOnline(steamData.value(Config::MARATHON_ID, -1));
    if (!steamError.isEmpty())
    {
        marOnline = "N/A";
    }

    // Формируем короткое сообщение
    QString report = QString("📊 <b>ОНЛАЙН</b> (%1)\n").arg(timeStr);
    report += QString("<b>D2</b>: %1").arg(d2Online);
    report += QString(" | <b>Marathon</b>: %1").arg(marOnline);

    return report;
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
        btn["text"] = "📊 Платформы";
        btn["callback_data"] = QString("platforms:%1").arg(requestId);
        row.append(btn);
        keyboardRows.append(row);
    }

    QJsonObject replyMarkup;
    replyMarkup["inline_keyboard"] = keyboardRows;
    return replyMarkup;
}

// --- Platforms ---
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
    if (!m_pendingRequests.contains(requestId)) return;
    RequestContext ctx = m_pendingRequests[requestId];

    int totalPlayers = 0;
    for (int count : platformStats) totalPlayers += count;

#ifdef USE_GUI_CHARTS
    bool useGui = Config::USE_GUI; // Или проверяй переменную окружения
#else
    bool useGui = false; // Всегда текст если GUI не скомпилирован
#endif

    if (useGui)
    {
#ifdef USE_GUI_CHARTS
        QImage chart = generatePlatformChart(platformStats, totalPlayers);

        QByteArray pngData;
        QBuffer buffer(&pngData);
        buffer.open(QIODevice::WriteOnly);
        chart.save(&buffer, "PNG");
        buffer.close();

        QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
        QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

        // [+] UPDATED Caption with 30-day disclaimer
        QString caption = QString(
            "📊 <b>РАСПРЕДЕЛЕНИЕ ПО ПЛАТФОРМАМ</b>\n"
            "🕒 %1\n"
            "<i>📈 Данные из Popularity.report — оценка за последние 30 дней.</i>\n"
            "<i>⚠️ Цифры примерные, не воспринимайте их сверхбуквально.</i>"
        ).arg(timeStr);

        m_tg->sendPhoto(ctx.chatId, pngData, caption, ctx.topicId);
#endif
    }
    else
    {
        // use full text report or compact
        QString report = /*generateTextPlatformReport(platformStats, totalPlayers);*/
                        generateCompactPlatformReport(platformStats, totalPlayers);
        m_tg->sendMessage(ctx.chatId, report, ctx.topicId);
    }

    QTimer::singleShot(300000, this, [this, requestId]() {
        m_pendingRequests.remove(requestId);
    });
    m_fetching = false;
}

#ifdef USE_GUI_CHARTS
QImage BotManager::generatePlatformChart(const QMap<PlatformCategory, int>& platformStats, int totalPlayers)
{
    const int width = 800;
    const int height = 950;
    QImage image(width, height, QImage::Format_RGB32);

    QColor bgColor(30, 32, 36);
    image.fill(bgColor);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // --- 1. Header ---
    QFont titleFont("Segoe UI", 24, QFont::Bold);
    painter.setFont(titleFont);
    painter.setPen(Qt::white);
    painter.drawText(0, 60, width, 50, Qt::AlignCenter, "📊 РАСПРЕДЕЛЕНИЕ ПО ПЛАТФОРМАМ");

    // --- 2. Colors ---
    const QMap<PlatformCategory, QColor> platformColors = {
        {PlatformCategory::PlayStation, QColor(0, 112, 243)},
        {PlatformCategory::Xbox,        QColor(16, 124, 16)},
        {PlatformCategory::Steam,       QColor(102, 192, 244)},
        {PlatformCategory::EpicGamesStore, QColor(255, 255, 255)},
        {PlatformCategory::Stadia,      QColor(233, 70, 70)}
    };

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore,
        PlatformCategory::Stadia
    };

    // --- 3. Chart (Donut) ---
    int pieSize = 340;
    int pieX = (width - pieSize) / 2;
    int pieY = 140;
    int centerX = width / 2;
    int centerY = pieY + (pieSize / 2);
    int radius = pieSize / 2;
    int ringWidth = 50;

    double currentAngle = -90;

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;

        int players = platformStats[cat];
        double percent = totalPlayers > 0 ? (players * 100.0 / totalPlayers) : 0;
        double spanAngle = percent * 3.6;

        QPainterPath path;
        QRectF outerRect(centerX - radius, centerY - radius, radius * 2, radius * 2);
        path.arcMoveTo(outerRect, currentAngle);
        path.arcTo(outerRect, currentAngle, spanAngle);

        QRectF innerRect(centerX - (radius - ringWidth), centerY - (radius - ringWidth), (radius - ringWidth) * 2, (radius - ringWidth) * 2);
        path.arcTo(innerRect, currentAngle + spanAngle, -spanAngle);
        path.closeSubpath();

        painter.setBrush(platformColors[cat]);
        painter.setPen(Qt::NoPen);
        painter.drawPath(path);

        currentAngle += spanAngle;
    }

    // --- 4. Legend ---
    QFont legendFont("Segoe UI", 16);
    painter.setFont(legendFont);

    int legendStartY = pieY + pieSize + 40;
    int itemHeight = 45;

    int currentY = legendStartY;
    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;

        int players = platformStats[cat];
        double percent = totalPlayers > 0 ? (players * 100.0 / totalPlayers) : 0;
        QString name = platformCategoryToString(cat);

        painter.setBrush(platformColors[cat]);
        painter.setPen(Qt::NoPen);
        int boxX = 100;
        painter.drawRect(boxX, currentY + 8, 20, 20);

        painter.setPen(Qt::white);
        painter.drawText(boxX + 30, currentY + 24, name);

        QFont percentFont("Segoe UI", 18, QFont::Bold);
        painter.setFont(percentFont);
        painter.setPen(platformColors[cat].lighter(120));
        QString percentStr = QString("%1%").arg(QString::number(percent, 'f', 1));
        painter.drawText(width - 120, currentY + 24, percentStr);

        painter.setFont(legendFont);
        painter.setPen(Qt::white);

        currentY += itemHeight;
    }

    // --- 5.PC vs consoles war ---
    int pcPlayers = platformStats.value(PlatformCategory::Steam, 0) + platformStats.value(PlatformCategory::EpicGamesStore, 0);
    int consolePlayers = platformStats.value(PlatformCategory::PlayStation, 0) + platformStats.value(PlatformCategory::Xbox, 0);

    double pcPercent = totalPlayers > 0 ? (pcPlayers * 100.0 / totalPlayers) : 0;
    double consolePercent = totalPlayers > 0 ? (consolePlayers * 100.0 / totalPlayers) : 0;

    int summaryY = height - 120;

    painter.setBrush(QColor(45, 48, 52));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(100, summaryY, width - 200, 70, 10, 10);

    painter.setPen(platformColors[PlatformCategory::Steam]);
    QFont summaryFont("Segoe UI", 16, QFont::Bold);
    painter.setFont(summaryFont);
    painter.drawText(120, summaryY + 30, QString("💻 ПК: %1%").arg(QString::number(pcPercent, 'f', 1)));

    painter.setPen(platformColors[PlatformCategory::PlayStation]);
    painter.drawText(width - 260, summaryY + 30, QString("🎮 Консоли: %1%").arg(QString::number(consolePercent, 'f', 1)));

    painter.end();
    return image;
}
#endif

QString BotManager::generateTextPlatformReport(const QMap<PlatformCategory, int>& platformStats, int totalPlayers)
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    QString report = QString(
        "📊 <b>РАСПРЕДЕЛЕНИЕ ПО ПЛАТФОРМАМ</b>\n"
        "🕒 %1\n"
        "━━━━━━━━━━━━━━━━━━━━\n\n"
    ).arg(timeStr);

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore,
        PlatformCategory::Stadia
    };

    // Эмодзи для платформ
    const QMap<PlatformCategory, QString> platformEmoji = {
        {PlatformCategory::PlayStation, "🟦"},
        {PlatformCategory::Xbox,        "🟩"},
        {PlatformCategory::Steam,       "🔵"},
        {PlatformCategory::EpicGamesStore, "⬜"},
        {PlatformCategory::Stadia,      "🟥"}
    };

    // Генерация списка платформ с прогресс-барами
    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;

        int players = platformStats[cat];
        double percent = totalPlayers > 0 ? (players * 100.0 / totalPlayers) : 0;
        QString name = platformCategoryToString(cat);
        QString emoji = platformEmoji.value(cat, "⬜");

        // Прогресс-бар из 10 блоков
        int filledBlocks = static_cast<int>(percent / 10);
        QString bar;
        for (int i = 0; i < 10; ++i)
        {
            bar += (i < filledBlocks) ? "🟩" : "⬜";
        }

        // [+] ONLY PERCENT, no player count
        report += QString("<b>%1 %2</b>\n%3\n")
                      .arg(emoji)
                      .arg(name)
                      .arg(bar);

        report += QString("<i>📈 %1%</i>\n\n")
                      .arg(QString::number(percent, 'f', 1));
    }

    // --- Сводка ПК vs Консоли (только проценты) ---
    int pcPlayers = platformStats.value(PlatformCategory::Steam, 0) + platformStats.value(PlatformCategory::EpicGamesStore, 0);
    int consolePlayers = platformStats.value(PlatformCategory::PlayStation, 0) + platformStats.value(PlatformCategory::Xbox, 0);

    double pcPercent = totalPlayers > 0 ? (pcPlayers * 100.0 / totalPlayers) : 0;
    double consolePercent = totalPlayers > 0 ? (consolePlayers * 100.0 / totalPlayers) : 0;

    report += QString("━━━━━━━━━━━━━━━━━━━━\n\n")
              + QString("<b>💻 ПЛАТФОРМЫ:</b>\n\n")
              + QString("<b>💻 ПК (Steam + EGS)</b>\n")
              + QString("📊 <b>%1%</b>\n\n")  // [+] Only percent
                  .arg(QString::number(pcPercent, 'f', 1))
              + QString("<b>🎮 Консоли (PS + Xbox)</b>\n")
              + QString("📊 <b>%1%</b>\n\n")  // [+] Only percent
                  .arg(QString::number(consolePercent, 'f', 1));

    // Дисклеймер
    report += QString("<i>📈 Данные из Popularity.report — оценка за последние 30 дней.</i>\n")
              + QString("<i>⚠️ Цифры примерные, не воспринимайте их сверхбуквально.</i>");

    return report;
}

QString BotManager::generateCompactPlatformReport(const QMap<PlatformCategory, int>& platformStats, int totalPlayers)
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    QString report;
    report += "📊 <b>РАСПРЕДЕЛЕНИЕ ПО ПЛАТФОРМАМ</b>\n";
    report += "🕒 " + timeStr + "\n";
    report += "━━━━━━━━━━━━━━━━━━━━\n\n";

    // Detail bar
    const QMap<PlatformCategory, QString> blockColors = {
        {PlatformCategory::PlayStation,     QString::fromUtf8("🟦")},
        {PlatformCategory::Xbox,            QString::fromUtf8("🟩")},
        {PlatformCategory::Steam,           QString::fromUtf8("🟥")},
        {PlatformCategory::EpicGamesStore,  QString::fromUtf8("⬜")},
        {PlatformCategory::Stadia,          QString::fromUtf8("🟨")}
    };

    // short bar (10 блоков)
    const QString consoleBlock = QString::fromUtf8("🟦");
    const QString pcBlock = QString::fromUtf8("🟥");
    const QString emptyBlock = QString::fromUtf8("⬛");

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore,
        PlatformCategory::Stadia
    };

    // === Detail ===
    const int DETAIL_BLOCKS = 30;
    QStringList detailBlocks;

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;
        double percent = totalPlayers > 0 ? (platformStats[cat] * 100.0 / totalPlayers) : 0;
        int blocks = qRound((percent / 100.0) * DETAIL_BLOCKS);
        if (blocks == 0 && percent > 0.5) blocks = 1;
        for (int i = 0; i < blocks; ++i)
            detailBlocks.append(blockColors.value(cat, emptyBlock));
    }
    while (detailBlocks.size() > DETAIL_BLOCKS) detailBlocks.removeLast();
    while (detailBlocks.size() < DETAIL_BLOCKS) detailBlocks.append(emptyBlock);
    QString detailBar = detailBlocks.join("");

    // === Short ===
    const int SUMMARY_BLOCKS = 10;
    QStringList summaryBlocks;

    int consolePlayers = platformStats.value(PlatformCategory::PlayStation, 0) +
                         platformStats.value(PlatformCategory::Xbox, 0);
    int pcPlayers = platformStats.value(PlatformCategory::Steam, 0) +
                    platformStats.value(PlatformCategory::EpicGamesStore, 0);

    double consolePercent = totalPlayers > 0 ? (consolePlayers * 100.0 / totalPlayers) : 0;
    double pcPercent = totalPlayers > 0 ? (pcPlayers * 100.0 / totalPlayers) : 0;

    int consoleBlocks = qRound((consolePercent / 100.0) * SUMMARY_BLOCKS);
    int pcBlocks = qRound((pcPercent / 100.0) * SUMMARY_BLOCKS);

    if (consoleBlocks == 0 && consolePercent > 0.5) consoleBlocks = 1;
    if (pcBlocks == 0 && pcPercent > 0.5) pcBlocks = 1;

    if (consoleBlocks + pcBlocks > SUMMARY_BLOCKS)
    {
        if (consoleBlocks > pcBlocks) pcBlocks = SUMMARY_BLOCKS - consoleBlocks;
        else consoleBlocks = SUMMARY_BLOCKS - pcBlocks;
    }

    for (int i = 0; i < consoleBlocks; ++i) summaryBlocks.append(consoleBlock);
    for (int i = 0; i < pcBlocks; ++i) summaryBlocks.append(pcBlock);

    while (summaryBlocks.size() > SUMMARY_BLOCKS) summaryBlocks.removeLast();
    while (summaryBlocks.size() < SUMMARY_BLOCKS) summaryBlocks.append(emptyBlock);
    QString summaryBar = summaryBlocks.join("");


    report += "<b>Платформы:</b>\n";
    report += detailBar + "\n\n";

    report += "<b>Детализация:</b>\n";
    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;
        double percent = totalPlayers > 0 ? (platformStats[cat] * 100.0 / totalPlayers) : 0;
        QString name = platformCategoryToString(cat);
        QString colorBlock = blockColors.value(cat, emptyBlock);
        report += colorBlock + " <b>" + name + "</b>: " + QString::number(percent, 'f', 1) + "%\n";
    }
    report += "\n";

    report += "<b>ПК / Консоли:</b>\n";
    report += summaryBar + "\n\n";

    report += "<b>Сводка:</b>\n";
    report += consoleBlock + " <b>Консоли (PS + Xbox)</b>: " + QString::number(consolePercent, 'f', 1) + "%\n";
    report += pcBlock + " <b>ПК (Steam + EGS)</b>: " + QString::number(pcPercent, 'f', 1) + "%\n\n";

    report += "━━━━━━━━━━━━━━━━━━━━\n";
    report += "<i>📈 Данные из Popularity.report — оценка за последние 30 дней.</i>\n";
    report += "<i>⚠️ Цифры примерные, не воспринимайте их сверхбуквально.</i>";

    return report;
}


// ==================== SCHEDULER ====================

void BotManager::scheduleTick()
{
    if (Config::TARGET_CHAT_ID != 0)
    {
        RequestContext ctx;
        ctx.chatId = Config::TARGET_CHAT_ID;
        ctx.topicId = 0; // General topic

        qDebug() << "[BotManager] Scheduled broadcast triggered.";
        fetchAndBroadcast(ctx);
    }

    // Explicitly schedule for TOMORROW to avoid race condition around target time
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QDateTime now = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    QDateTime nextTarget = now;
    nextTarget.setTime(Config::SCHEDULE_TIME);
    nextTarget = nextTarget.addDays(1); // Force next day

    m_scheduleTimer.start(now.msecsTo(nextTarget));
    qDebug() << "[BotManager] Timer reset. Next run:" << nextTarget.toString(Qt::ISODate);
}

qint64 BotManager::msecToNextScheduledTime()
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QDateTime now = QDateTime::currentDateTimeUtc().toTimeZone(tz);
    QDateTime target = now;
    target.setTime(Config::SCHEDULE_TIME);

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