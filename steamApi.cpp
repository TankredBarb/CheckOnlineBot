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
    // Create a container for results that will be shared across all replies for THIS requestId
    // We use a shared pointer-like approach with a QObject to manage its lifetime
    auto* requestState = new QObject(this);
    requestState->setProperty("requestId", requestId);
    requestState->setProperty("pendingCount", appIds.size());
    requestState->setProperty("results", QVariant::fromValue(QMap<int, int>()));

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
        reply->setProperty("statePtr", QVariant::fromValue(requestState));
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
    if (!reply) return;

    int appId = reply->property("appId").toInt();
    auto* requestState = reply->property("statePtr").value<QObject*>();
    
    if (!requestState)
    {
        reply->deleteLater();
        return;
    }

    int requestId = requestState->property("requestId").toInt();
    int pendingCount = requestState->property("pendingCount").toInt();
    QMap<int, int> results = requestState->property("results").value<QMap<int, int>>();

    QString error;
    int players = parsePlayerCount(reply, error);

    if (players >= 0)
    {
        results[appId] = players;
        requestState->setProperty("results", QVariant::fromValue(results));
    }

    reply->deleteLater();

    pendingCount--;
    requestState->setProperty("pendingCount", pendingCount);

    if (pendingCount <= 0)
    {
        emit playersDataReady(results, error, requestId);
        requestState->deleteLater();
    }
}
