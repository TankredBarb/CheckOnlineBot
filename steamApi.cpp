#include "steamApi.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QDebug>

SteamApi::SteamApi(QObject* parent)
    : QObject(parent)
{
}

void SteamApi::requestCurrentPlayers(const QList<int>& appIds)
{
    m_expectedCount = appIds.size();
    m_tempResults.clear();

    for (int id : appIds)
    {
        QString urlString = QString("https://api.steampowered.com/ISteamUserStats/GetNumberOfCurrentPlayers/v1/"
                                    "?appid=%1&format=json").arg(id);

        QUrl url(urlString);
        QNetworkRequest request(url);

        request.setTransferTimeout(5000);

        QNetworkReply* reply = m_net.get(request);
        connect(reply, &QNetworkReply::finished, this, &SteamApi::onSteamReplyFinished);
    }
}

void SteamApi::onSteamReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
    {
        return;
    }

    QUrlQuery query(reply->url());
    int appId = query.queryItemValue("appid").toInt();

    int players = -1;

    if (reply->error() == QNetworkReply::NoError)
    {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (!doc.isNull() && doc.isObject())
        {
            QJsonObject root = doc.object();
            if (root.contains("response"))
            {
                QJsonObject response = root["response"].toObject();
                if (response.contains("player_count"))
                {
                    players = response["player_count"].toInt();
                }
            }
        }
    }
    else
    {
        qWarning() << "Steam API Error for AppID" << appId << ":" << reply->errorString();
    }

    m_tempResults[appId] = players;
    reply->deleteLater();

    if (--m_expectedCount <= 0)
    {
        emit playersDataReady(m_tempResults);
    }
}