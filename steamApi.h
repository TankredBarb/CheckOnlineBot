#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QMap>

// Steam API configuration constants
namespace SteamApiConfig {
    constexpr const char* BaseUrl = "https://api.steampowered.com/ISteamUserStats/GetNumberOfCurrentPlayers/v1/";
    constexpr const char* PlayerCountField = "player_count";
    constexpr int RequestTimeoutMs = 8000;
}

class SteamApi : public QObject
{
    Q_OBJECT
public:
    explicit SteamApi(QObject* parent = nullptr);

    // Requests current player counts for multiple app IDs
    // Emits playersDataReady when all requests are completed
    void requestCurrentPlayers(const QList<int>& appIds, int requestId);

signals:
    void playersDataReady(const QMap<int, int>& data, const QString& error, int requestId);

private slots:
    void onSteamReplyFinished();

private:
    // Parses Steam API response and returns player count or error message
    int parsePlayerCount(QNetworkReply* reply, QString& error);

    QNetworkAccessManager m_net;
    int m_pendingRequestsCount = 0;      // Number of pending API responses
    QMap<int, int> m_pendingResults;     // Accumulated results: appId -> playerCount
    int m_currentRequestId = 0;          // ID of the current batch request
};