#include "popularityApi.h"
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>

PopularityApi::PopularityApi(QObject* parent)
    : QObject(parent)
{
}

void PopularityApi::setStandardHeaders(QNetworkRequest& request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, Config::USER_AGENT);
    request.setRawHeader("x-api-key", Config::POPULARITY_API_KEY.toUtf8());
    request.setRawHeader("accept", "*/*");
    request.setRawHeader("accept-language", "ru;q=0.6");
    request.setRawHeader("cache-control", "no-cache");
    request.setRawHeader("origin", "https://popularity.report");
    request.setRawHeader("pragma", "no-cache");
    request.setRawHeader("referer", "https://popularity.report/");
    request.setRawHeader("sec-ch-ua", Config::SEC_CH_UA.toUtf8());
    request.setRawHeader("sec-ch-ua-mobile", "?0");
    request.setRawHeader("sec-ch-ua-platform", "\"Windows\"");
    request.setRawHeader("sec-fetch-dest", "empty");
    request.setRawHeader("sec-fetch-mode", "cors");
    request.setRawHeader("sec-fetch-site", "same-site");
    request.setRawHeader("sec-gpc", "1");
}

void PopularityApi::requestCrossPlatformPlayer(const QString& gameSlug, int requestId)
{
    m_currentRequestId = requestId;
    m_currentSlug = gameSlug;

    QUrl url(Config::POPULARITY_API_URL);
    QNetworkRequest request(url);

    setStandardHeaders(request);
    request.setTransferTimeout(8000);

    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, &PopularityApi::onPopularityReplyFinished);
}

void PopularityApi::onPopularityReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    QString error;
    int players = -1;

    if (reply->error() != QNetworkReply::NoError)
    {
        error = QString("Network error: %1").arg(reply->errorString());
    }
    else
    {
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpCode != 200)
        {
            error = QString("Popularity server returned status %1").arg(httpCode);
        }
        else
        {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isNull() || !doc.isArray() || doc.array().isEmpty())
            {
                error = "Invalid data format from Popularity.report";
            }
            else
            {
                QJsonObject latest = doc.array().last().toObject();
                if (latest.contains("players") && latest["players"].isDouble())
                {
                    players = static_cast<int>(latest["players"].toDouble());
                }
                else
                {
                    error = "Popularity API did not return players field";
                }
            }
        }
    }

    reply->deleteLater();
    emit popularityDataReady(players, error, m_currentSlug, m_currentRequestId);
}

void PopularityApi::requestPlatformDistribution()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QUrl url(QString("%1/platforms?before=%2").arg(Config::POPULARITY_API_BASE_URL).arg(now));

    QNetworkRequest request(url);
    setStandardHeaders(request);
    request.setTransferTimeout(8000);

    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, &PopularityApi::onPlatformDistributionFinished);
}

void PopularityApi::onPlatformDistributionFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    // 1. Network error check
    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "[PopularityApi] Platform distribution network error:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    // 2. HTTP status check
    int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200)
    {
        qWarning() << "[PopularityApi] Platform distribution HTTP error:" << httpCode;
        reply->deleteLater();
        return;
    }

    // 3. JSON validation
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isArray())
    {
        qWarning() << "[PopularityApi] Platform distribution: Invalid or non-array JSON";
        reply->deleteLater();
        return;
    }

    QJsonArray array = doc.array();

    // 4. Parse & map data
    QMap<PlatformCategory, int> platformStats;
    platformStats[PlatformCategory::Xbox] = 0;
    platformStats[PlatformCategory::PlayStation] = 0;
    platformStats[PlatformCategory::Steam] = 0;
    platformStats[PlatformCategory::Stadia] = 0;
    platformStats[PlatformCategory::EpicGamesStore] = 0;

    for (const QJsonValue& val : array)
    {
        QJsonObject obj = val.toObject();
        int categoryId = obj["category"].toInt();
        int players = obj["players"].toInt();

        if (categoryId < 1 || categoryId > 6 || categoryId == 4)
            continue;

        PlatformCategory cat = static_cast<PlatformCategory>(categoryId);
        platformStats[cat] += players;
    }

    // 5. Console output for testing
    int totalPlayers = 0;
    for (int count : platformStats) totalPlayers += count;

    qDebug() << "\n[Platform Distribution Summary]";
    qDebug() << "----------------------------------";
    for (auto it = platformStats.begin(); it != platformStats.end(); ++it)
    {
        PlatformCategory cat = it.key();
        int players = it.value();
        double percent = totalPlayers > 0 ? (players * 100.0 / totalPlayers) : 0;

        qDebug() << QString("%1 (ID:%2): %3 players (%4%)")
                    .arg(platformCategoryToString(cat))
                    .arg(static_cast<int>(cat))
                    .arg(players)
                    .arg(QString::number(percent, 'f', 2));
    }
    qDebug() << "----------------------------------";
    qDebug() << "Total:" << totalPlayers << "\n";

    // 6. Emit to caller
    emit platformDistributionReceived(platformStats);

    reply->deleteLater();
}