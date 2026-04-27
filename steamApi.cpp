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
    m_pendingRequestsCount = appIds.size();
    m_pendingResults.clear();
    m_currentRequestId = requestId;

    for (int appId : appIds)
    {
        QUrl url(SteamApiConfig::BaseUrl);
        QUrlQuery query;
        query.addQueryItem("appid", QString::number(appId));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setTransferTimeout(SteamApiConfig::RequestTimeoutMs);

        QNetworkReply* reply = m_net.get(request);
        reply->setProperty("appId", appId);
        connect(reply, &QNetworkReply::finished, this, &SteamApi::onSteamReplyFinished);
    }
}

int SteamApi::parsePlayerCount(QNetworkReply* reply, QString& error)
{
    int players = -1;

    if (reply->error() != QNetworkReply::NoError)
    {
        error = QString("Network error: %1").arg(reply->errorString());
        return players;
    }

    int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200)
    {
        error = QString("Steam server returned status %1").arg(httpCode);
        return players;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject())
    {
        error = "Invalid data format from Steam";
        return players;
    }

    QJsonObject root = doc.object();
    QJsonObject response = root["response"].toObject();
    if (response.contains(SteamApiConfig::PlayerCountField) &&
        response[SteamApiConfig::PlayerCountField].isDouble())
    {
        players = static_cast<int>(response[SteamApiConfig::PlayerCountField].toDouble());
    }
    else
    {
        error = "Steam API did not return player_count field";
    }

    return players;
}

void SteamApi::onSteamReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    int appId = reply->property("appId").toInt();
    QString error;

    int players = parsePlayerCount(reply, error);

    if (players >= 0)
    {
        m_pendingResults[appId] = players;
    }

    reply->deleteLater();

    if (--m_pendingRequestsCount <= 0)
    {
        emit playersDataReady(m_pendingResults, error, m_currentRequestId);
    }
}