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

    // Skip Popularity API for ShortStats requests
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

    // Only Steam data needed for ShortStats
    if (ctx.type == RequestContext::RequestType::ShortStats)
    {
        if (steamReady)
        {
            sendShortReport(requestId);
        }
        return;
    }

    // PlayerCount requires both Steam and Popularity (if key is set)
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
        QString hostInfo = QString("%1 %2 %3").arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildAbi()).arg(QSysInfo::kernelType());

        const QString separator = "━━━━━━━━━━━━━━━━━━━━";

        QString reply = QString(
            "⏱ <b>СТАТУС: UPTIME</b>\n"
            "%1\n"
            "🕐 Работает: <b>%2</b>\n"
            "📅 Запуск: <code>%3</code>\n"
            "🖥 Хост: <code>%4</code>\n"
            "%5\n"
            "<i>Все данные по (UTC&#43;3)</i>"
        ).arg(separator).arg(uptimeStr).arg(localStart).arg(hostInfo).arg(separator);

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
    if (!m_pendingRequests.contains(requestId))
        return;

    RequestContext ctx = m_pendingRequests[requestId];
    auto steamData = m_steamCache.take(requestId);
    
    // Ensure we use -1 if the result is missing from cache
    int popData = m_popularityCache.contains(requestId) ? m_popularityCache.take(requestId) : -1;

    QString steamErr = m_steamErrorCache.take(requestId);
    QString popErr = m_popErrorCache.take(requestId);

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
    // Helper to format numbers with spaces and wrap in <code> for monospaced look
    auto fmt = [](int val) -> QString {
        if (val < 0) return "<code>N/A</code>";
        QString s = QString::number(val);
        for (int i = s.length() - 3; i > 0; i -= 3)
            s.insert(i, " ");
        return "<code>" + s + "</code>";
    };

    const QString d2Link = "https://store.steampowered.com/app/1085660";
    const QString marLink = "https://store.steampowered.com/app/3065800";

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    bool isRecentReset = isDestiny2ResetRecent();
    QString resetNote = isRecentReset ? "\n🔴 <b>Внимание:</b> Недавно был рисет!" : "";

    QString d2Section;
    if (!steamError.isEmpty() && !popError.isEmpty())
    {
        d2Section = QString(
            "🎮 <b>DESTINY 2</b>\n"
            "🔴 <b>Данные недоступны</b>\n"
            "<i>Ошибка API: %1</i>"
        ).arg(steamError);
    }
    else
    {
        d2Section = QString("🎮 <b>DESTINY 2</b>\n");
        
        if (steamError.isEmpty())
        {
            d2Section += QString("├─ 💻 Steam Live: %1\n").arg(fmt(steamData.value(Config::DESTINY_ID, -1)));
        }
        else
        {
            d2Section += "├─ 💻 Steam Live: <code>Error</code>\n";
        }

        if (popError.isEmpty() && destinyAllPlatforms >= 0)
        {
            d2Section += QString("└─ 🌍 Global 24h: ~%1\n").arg(fmt(destinyAllPlatforms));
            d2Section += "   <i>(PS, Xbox, PC, Epic)</i>";
        }
        else
        {
            d2Section += "└─ 🌍 Global 24h: <code>N/A</code>";
        }
        
        d2Section += resetNote;
    }

    QString marSection;
    if (steamError.isEmpty())
    {
        marSection = QString(
            "🌌 <b>MARATHON</b>\n"
            "└─ 💻 Steam Live: %1"
        ).arg(fmt(steamData.value(Config::MARATHON_ID, -1)));
    }
    else
    {
        marSection = "🌌 <b>MARATHON</b>\n└─ 💻 Steam Live: <code>Error</code>";
    }

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";

    return QString(
        "📊 <b>СВОДКА: ОНЛАЙН BUNGIE</b>\n"
        "<i>Прямой эфир и суточные оценки</i>\n"
        "\n%1\n"
        "🕒 %2\n"
        "🌐 <a href=\"%3\">Просмотр на Bungie.net</a>\n"
        "\n%4\n"
        "\n%5\n"
        "\n%6\n"
        "📈 <i>Глобальные данные: Popularity.report (24ч).</i>\n"
        "⚠️ <i>Цифры примерные, не воспринимайте их буквально.</i>\n"
        "⏱ <i>Кнопки ниже активны 5 минут.</i>"
    ).arg(separator)
     .arg(timeStr)
     .arg(Config::BUNGIE_PREVIEW_URL)
     .arg(d2Section)
     .arg(marSection)
     .arg(separator);
}

QString BotManager::formatShortReport(const QMap<int, int>& steamData, const QString& steamError,
                                      int destinyAllPlatforms, const QString& popError)
{
    auto fmt = [](int val) -> QString {
        if (val < 0) return "<code>N/A</code>";
        QString s = QString::number(val);
        for (int i = s.length() - 3; i > 0; i -= 3)
            s.insert(i, " ");
        return "<code>" + s + "</code>";
    };

    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm");

    QString d2Online = steamError.isEmpty() ? fmt(steamData.value(Config::DESTINY_ID, -1)) : "<code>Error</code>";
    QString marOnline = steamError.isEmpty() ? fmt(steamData.value(Config::MARATHON_ID, -1)) : "<code>Error</code>";

    return QString("📊 <b>ОНЛАЙН</b> [<code>%1</code>]\n"
                   "<b>D2</b>: %2 | <b>MAR</b>: %3")
            .arg(timeStr)
            .arg(d2Online)
            .arg(marOnline);
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
    RequestContext ctx = m_pendingRequests.take(requestId);

    int totalPlayers = 0;
    for (int count : platformStats) totalPlayers += count;


#ifdef USE_GUI_CHARTS
    bool useGui = Config::USE_GUI; // Or check env variable
#else
    bool useGui = false; // Always text if GUI not compiled
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
}

#ifdef USE_GUI_CHARTS
QImage BotManager::generatePlatformChart(const QMap<PlatformCategory, int>& platformStats, int totalPlayers)
{
    const int width = 800;
    const int height = 950;
    QImage image(width, height, QImage::Format_RGB32);

    // --- 1. Background: Deep Tech Gradient ---
    QRadialGradient bgGradient(width / 2, height / 2, width);
    bgGradient.setColorAt(0, QColor(45, 48, 56));
    bgGradient.setColorAt(1, QColor(20, 22, 25));
    
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(image.rect(), bgGradient);

    // --- 2. Header: Bungie Style ---
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
    painter.drawLine(100, 100, width - 100, 100);
    
    QFont titleFont("Segoe UI", 28, QFont::Light);
    painter.setFont(titleFont);
    painter.setPen(Qt::white);
    painter.drawText(0, 40, width, 60, Qt::AlignCenter, "РАСПРЕДЕЛЕНИЕ ИГРОКОВ");

    // --- 3. Modern Color Palette ---
    const QMap<PlatformCategory, QColor> baseColors = {
        {PlatformCategory::PlayStation, QColor(0, 112, 243)},
        {PlatformCategory::Xbox,        QColor(16, 124, 16)},
        {PlatformCategory::Steam,       QColor(255, 140, 0)}, 
        {PlatformCategory::EpicGamesStore, QColor(220, 220, 220)}
    };

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore
    };

    // --- 4. Donut Chart: Modular Segments ---
    int pieSize = 400;
    int centerX = width / 2;
    int centerY = 320;
    int radius = pieSize / 2;
    int ringWidth = 60;
    const double GAP = 2.0; // Gaps between segments

    double currentAngle = -90;

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;

        double percent = totalPlayers > 0 ? (platformStats[cat] * 100.0 / totalPlayers) : 0;
        if (percent < 0.5) continue;

        double spanAngle = (percent * 3.6) - (GAP);

        QPainterPath path;
        QRectF outerRect(centerX - radius, centerY - radius, radius * 2, radius * 2);
        path.arcMoveTo(outerRect, currentAngle + (GAP / 2.0));
        path.arcTo(outerRect, currentAngle + (GAP / 2.0), spanAngle);

        QRectF innerRect(centerX - (radius - ringWidth), centerY - (radius - ringWidth), (radius - ringWidth) * 2, (radius - ringWidth) * 2);
        path.arcTo(innerRect, currentAngle + (GAP / 2.0) + spanAngle, -spanAngle);
        path.closeSubpath();

        // Gradient for depth
        QConicalGradient segmentGradient(centerX, centerY, currentAngle + (spanAngle / 2.0));
        QColor base = baseColors[cat];
        segmentGradient.setColorAt(0, base.lighter(120));
        segmentGradient.setColorAt(1, base.darker(120));

        painter.setBrush(segmentGradient);
        painter.setPen(Qt::NoPen);
        painter.drawPath(path);

        currentAngle += spanAngle + GAP;
    }

    // --- 5. Legend: Clean Cards ---
    int currentY = 560;
    int cardWidth = 600;
    int cardX = (width - cardWidth) / 2;

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;

        int players = platformStats[cat];
        double percent = totalPlayers > 0 ? (players * 100.0 / totalPlayers) : 0;
        QString name = platformCategoryToString(cat).toUpper();

        // Card BG
        painter.setBrush(QColor(255, 255, 255, 10));
        painter.drawRoundedRect(cardX, currentY, cardWidth, 60, 5, 5);

        // Indicator
        painter.setBrush(baseColors[cat]);
        painter.drawRect(cardX + 10, currentY + 15, 6, 30);

        // Text
        painter.setPen(Qt::white);
        painter.setFont(QFont("Segoe UI", 16, QFont::Medium));
        painter.drawText(cardX + 30, currentY + 38, name);

        // Percentage
        painter.setFont(QFont("Segoe UI", 18, QFont::Bold));
        painter.setPen(baseColors[cat].lighter(130));
        painter.drawText(cardX + cardWidth - 100, currentY + 40, QString("%1%").arg(QString::number(percent, 'f', 1)));

        currentY += 75;
    }

    // --- 6. PC vs Consoles: Comparison Bar (Lowered to avoid overlap) ---
    int pcPlayers = platformStats.value(PlatformCategory::Steam, 0) + platformStats.value(PlatformCategory::EpicGamesStore, 0);
    int consolePlayers = platformStats.value(PlatformCategory::PlayStation, 0) + platformStats.value(PlatformCategory::Xbox, 0);
    double pcPct = totalPlayers > 0 ? (pcPlayers * 100.0 / totalPlayers) : 0;
    double consolePct = totalPlayers > 0 ? (consolePlayers * 100.0 / totalPlayers) : 0;

    int barY = height - 70; // Moved lower
    int barWidth = 600;
    int barX = (width - barWidth) / 2;
    int barH = 12;

    // Background bar
    painter.setBrush(QColor(255, 255, 255, 20));
    painter.drawRoundedRect(barX, barY, barWidth, barH, 6, 6);

    // Console segment (Left)
    int conW = (consolePct / 100.0) * barWidth;
    painter.setBrush(baseColors[PlatformCategory::PlayStation]);
    painter.drawRoundedRect(barX, barY, conW, barH, 6, 6);

    // PC segment (Right)
    int pcW = (pcPct / 100.0) * barWidth;
    painter.setBrush(baseColors[PlatformCategory::Steam]);
    painter.drawRoundedRect(barX + barWidth - pcW, barY, pcW, barH, 6, 6);

    // Labels (Perfectly aligned BELOW the bar)
    painter.setFont(QFont("Segoe UI", 12, QFont::Bold));
    int labelY = barY + barH + 10;
    int labelHeight = 30;

    painter.setPen(baseColors[PlatformCategory::PlayStation].lighter(150));
    painter.drawText(barX, labelY, 200, labelHeight, Qt::AlignLeft | Qt::AlignVCenter, "КОНСОЛИ");
    
    painter.setPen(baseColors[PlatformCategory::Steam].lighter(150));
    painter.drawText(barX + barWidth - 200, labelY, 200, labelHeight, Qt::AlignRight | Qt::AlignVCenter, "ПК");

    painter.end();
    return image;
}
#endif

QString BotManager::generateTextPlatformReport(const QMap<PlatformCategory, int>& platformStats, int totalPlayers)
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    auto fmtPct = [](double pct) -> QString {
        return "<code>" + QString::number(pct, 'f', 1).rightJustified(4, ' ') + "%</code>";
    };

    auto fmtNum = [](int val) -> QString {
        QString s = QString::number(val);
        for (int i = s.length() - 3; i > 0; i -= 3) s.insert(i, " ");
        return "<code>" + s + "</code>";
    };

    const QMap<PlatformCategory, QString> platformIcons = {
        {PlatformCategory::PlayStation,     "🟦"}, 
        {PlatformCategory::Xbox,            "🟩"}, 
        {PlatformCategory::Steam,           "🟧"}, 
        {PlatformCategory::EpicGamesStore,  "⬜"}
    };

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore
    };

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";
    const QString subSep = "────────────────────";

    QString report = QString(
        "📊 <b>ДЕТАЛЬНЫЙ ОТЧЕТ: ПЛАТФОРМЫ</b>\n"
        "%1\n"
        "🕒 <b>Обновлено:</b> %2\n"
        "👥 <b>Всего игроков:</b> %3\n\n"
    ).arg(separator).arg(timeStr).arg(fmtNum(totalPlayers));

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;

        int players = platformStats[cat];
        double pct = (players * 100.0) / totalPlayers;
        QString name = platformCategoryToString(cat).toUpper();
        QString icon = platformIcons.value(cat, "▫️");

        // 10-block bar for each
        int filled = qRound(pct / 10.0);
        if (filled == 0 && pct > 1.0) filled = 1;
        QString bar;
        for (int i = 0; i < 10; ++i) bar += (i < filled) ? icon : "⬛";

        report += QString(
            "%1 <b>%2</b>\n"
            "%3 %4\n"
            "└─ %5 чел.\n\n"
        ).arg(icon).arg(name).arg(bar).arg(fmtPct(pct)).arg(fmtNum(players));
    }

    // Console vs PC Summary
    int consolePlayers = platformStats.value(PlatformCategory::PlayStation, 0) + platformStats.value(PlatformCategory::Xbox, 0);
    int pcPlayers = platformStats.value(PlatformCategory::Steam, 0) + platformStats.value(PlatformCategory::EpicGamesStore, 0);
    double conPct = (consolePlayers * 100.0) / totalPlayers;
    double pcPct = (pcPlayers * 100.0) / totalPlayers;

    report += QString(
        "%1\n"
        "<b>💻 ГЛОБАЛЬНЫЙ БАЛАНС:</b>\n"
        "🎮 КОНСОЛИ : %2\n"
        "💻 ПК      : %3\n"
        "%4\n"
        "<i>📈 Источник: Popularity.report</i>\n"
        "⚠️ <i>Примерные данные за 30 дней.</i>"
    ).arg(subSep).arg(fmtPct(conPct)).arg(fmtPct(pcPct)).arg(separator);

    return report;
}

QString BotManager::generateCompactPlatformReport(const QMap<PlatformCategory, int>& platformStats, int totalPlayers)
{
    QTimeZone tz(Config::KYIV_TIMEZONE.toUtf8());
    QString timeStr = QDateTime::currentDateTimeUtc().toTimeZone(tz).toString("HH:mm • dd.MM.yyyy");

    auto fmtPct = [](double pct) -> QString {
        QString s = QString::number(pct, 'f', 1);
        if (pct < 10.0) s = " " + s; // Simple padding for alignment
        return "<code>" + s + "%</code>";
    };

    // Emojis for platforms
    const QMap<PlatformCategory, QString> platformIcons = {
        {PlatformCategory::PlayStation,     "🟦"}, // Blue
        {PlatformCategory::Xbox,            "🟩"}, // Green
        {PlatformCategory::Steam,           "🟧"}, // Orange (better contrast with PS)
        {PlatformCategory::EpicGamesStore,  "⬜"}, // White
        {PlatformCategory::Stadia,          "💀"}  // RIP
    };

    const QList<PlatformCategory> order = {
        PlatformCategory::PlayStation,
        PlatformCategory::Xbox,
        PlatformCategory::Steam,
        PlatformCategory::EpicGamesStore
    };

    // 1. Calculate spectrum bar (Restored to 25 blocks for high impact)
    const int SPECTRUM_BLOCKS = 25;
    QString spectrumBar;
    int blocksUsed = 0;

    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;
        double pct = (platformStats[cat] * 100.0) / totalPlayers;
        int blocks = qRound((pct / 100.0) * SPECTRUM_BLOCKS);
        if (blocks == 0 && pct > 1.0) blocks = 1; 
        
        for (int i = 0; i < blocks && blocksUsed < SPECTRUM_BLOCKS; ++i)
        {
            spectrumBar += platformIcons.value(cat);
            blocksUsed++;
        }
    }
    while (blocksUsed < SPECTRUM_BLOCKS)
    {
        spectrumBar += "⬛";
        blocksUsed++;
    }

    // 2. Build detailed list (Removed mini-bars for mobile fit)
    QString detailLines;
    for (PlatformCategory cat : order)
    {
        if (!platformStats.contains(cat) || platformStats[cat] <= 0) continue;
        double pct = (platformStats[cat] * 100.0) / totalPlayers;
        
        detailLines += QString("%1 %2 : %3\n")
            .arg(platformIcons.value(cat))
            .arg(platformCategoryToString(cat).leftJustified(12, ' '))
            .arg(fmtPct(pct));
    }

    // 3. PC vs Consoles Duel
    int consolePlayers = platformStats.value(PlatformCategory::PlayStation, 0) + platformStats.value(PlatformCategory::Xbox, 0);
    int pcPlayers = platformStats.value(PlatformCategory::Steam, 0) + platformStats.value(PlatformCategory::EpicGamesStore, 0);
    double consolePct = (consolePlayers * 100.0) / totalPlayers;
    double pcPct = (pcPlayers * 100.0) / totalPlayers;

    const int DUEL_BLOCKS = 10;
    int consoleDuelBlocks = qRound((consolePct / 100.0) * DUEL_BLOCKS);
    if (consoleDuelBlocks == 0 && consolePct > 0) consoleDuelBlocks = 1;
    if (consoleDuelBlocks == DUEL_BLOCKS && pcPct > 0) consoleDuelBlocks = DUEL_BLOCKS - 1;
    
    QString duelBar;
    for (int i = 0; i < DUEL_BLOCKS; ++i) duelBar += (i < consoleDuelBlocks) ? "🟦" : "🟧";

    const QString separator = "━━━━━━━━━━━━━━━━━━━━";

    return QString(
        "📊 <b>РАСПРЕДЕЛЕНИЕ: DESTINY 2</b>\n"
        "%1\n"
        "🕒 %2\n"
        "\n"
        "<b>СПЕКТР ПЛАТФОРМ:</b>\n"
        "%3\n"
        "\n"
        "<b>ДЕТАЛИЗАЦИЯ:</b>\n"
        "%4\n"
        "<b>ДУЭЛЬ: КОНСОЛИ VS ПК</b>\n"
        "%5\n"
        "🎮 Consoles : [%6]\n"
        "💻 PC       : [%7]\n"
        "\n"
        "%8\n"
        "<i>📈 Данные: Popularity.report (30 дней).</i>\n"
        "⚠️ <i>Оценка примерная, погрешность ±5%.</i>"
    ).arg(separator)
     .arg(timeStr)
     .arg(spectrumBar)
     .arg(detailLines)
     .arg(duelBar)
     .arg(fmtPct(consolePct))
     .arg(fmtPct(pcPct))
     .arg(separator);
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

bool BotManager::isDestiny2ResetRecent() const
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QDate today = now.date();
    QTime resetTime(17, 0, 0);

    // Find the most recent Tuesday 17:00 UTC
    QDateTime lastReset;
    if (now.time() >= resetTime)
    {
        // Today's reset already happened if today is Tuesday or later in the week
        int daysSinceMonday = today.dayOfWeek() - 1; // Monday=0, Sunday=6
        if (today.dayOfWeek() == 2) // Today is Tuesday
        {
            lastReset = QDateTime(today, resetTime);
        }
        else if (today.dayOfWeek() > 2)
        {
            // Reset was on previous Tuesday
            lastReset = QDateTime(today.addDays(-(daysSinceMonday - 1)), resetTime);
        }
        else
        {
            // Reset was last Tuesday
            lastReset = QDateTime(today.addDays(-(daysSinceMonday + 6)), resetTime);
        }
    }
    else
    {
        // Today's reset hasn't happened yet, so last reset was previous Tuesday
        int daysSinceMonday = today.dayOfWeek() - 1;
        if (today.dayOfWeek() == 2) // Today is Tuesday, but before 17:00
        {
            lastReset = QDateTime(today.addDays(-7), resetTime);
        }
        else if (today.dayOfWeek() > 2)
        {
            lastReset = QDateTime(today.addDays(-(daysSinceMonday - 1)), resetTime);
        }
        else
        {
            lastReset = QDateTime(today.addDays(-(daysSinceMonday + 6)), resetTime);
        }
    }

    qint64 hoursSinceReset = lastReset.secsTo(now) / 3600.0;
    return (hoursSinceReset >= 0 && hoursSinceReset < 10);
}
