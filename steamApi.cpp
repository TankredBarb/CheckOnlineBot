#include "steamApi.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QUrlQuery>

SteamApi::SteamApi(QObject* parent) : QObject(parent)
{
}

void SteamApi::requestCurrentPlayers(const QList<int>& appIds, int requestId)
{
    m_expectedCount = appIds.size();
    m_tempPlayers.clear();
    m_currentRequestId = requestId;

    for (int appId : appIds)
    {
        QUrl url("https://api.steampowered.com/ISteamUserStats/GetNumberOfCurrentPlayers/v1/");
        QUrlQuery query;
        query.addQueryItem("appid", QString::number(appId));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setTransferTimeout(8000); // 8 second timeout

        QNetworkReply* reply = m_net.get(request);
        reply->setProperty("appId", appId);
        connect(reply, &QNetworkReply::finished, this, &SteamApi::onSteamReplyFinished);
    }
}

void SteamApi::onSteamReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    int appId = reply->property("appId").toInt();
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
            error = QString("Steam server returned status %1").arg(httpCode);
        }
        else
        {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isNull() || !doc.isObject())
            {
                error = "Invalid data format from Steam";
            }
            else
            {
                QJsonObject root = doc.object();
                QJsonObject response = root["response"].toObject();
                if (response.contains("player_count") && response["player_count"].isDouble())
                {
                    players = static_cast<int>(response["player_count"].toDouble());
                }
                else
                {
                    error = "Steam API did not return player_count field";
                }
            }
        }
    }

    if (players >= 0)
    {
        m_tempPlayers[appId] = players;
    }

    reply->deleteLater();

    if (--m_expectedCount <= 0)
    {
        emit playersDataReady(m_tempPlayers, error, m_currentRequestId);
    }
}