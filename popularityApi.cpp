#include "popularityApi.h"
#include "config.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

PopularityApi::PopularityApi(QObject* parent) : QObject(parent)
{
}

void PopularityApi::requestCrossPlatformPlayer(const QString& gameSlug, int requestId)
{
    m_currentRequestId = requestId;
    m_currentSlug = gameSlug;

    QUrl url(Config::POPULARITY_API_URL);
    QNetworkRequest request(url);

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