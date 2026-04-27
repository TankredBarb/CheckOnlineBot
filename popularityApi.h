#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include "config.h"

enum class PlatformCategory : int
{
    Xbox = 1,
    PlayStation = 2,
    Steam = 3,
    Stadia = 5,
    EpicGamesStore = 6
};

inline QString platformCategoryToString(PlatformCategory cat)
{
    switch (cat)
    {
        case PlatformCategory::Xbox: return "Xbox";
        case PlatformCategory::PlayStation: return "PlayStation";
        case PlatformCategory::Steam: return "Steam";
        case PlatformCategory::Stadia: return "Stadia";
        case PlatformCategory::EpicGamesStore: return "Epic Games Store";
        default: return "Unknown";
    }
}

inline bool isValidPlatformCategory(int categoryId)
{
    return (categoryId >= 1 && categoryId <= 6 && categoryId != 4);
}

class PopularityApi : public QObject
{
    Q_OBJECT

public:
    explicit PopularityApi(QObject* parent = nullptr);
    void requestCrossPlatformPlayer(const QString& gameSlug, int requestId);
    void requestPlatformDistribution(int requestId);

signals:
    void popularityDataReady(int players, const QString& error, QString gameSlug, int requestId);
    void platformDistributionReceived(const QMap<PlatformCategory, int>& platformStats, int requestId);

private slots:
    void onPopularityReplyFinished();
    void onPlatformDistributionFinished();

private:
    void setStandardHeaders(QNetworkRequest& request);
    QNetworkRequest createRequest(const QUrl& url);
    bool validateJsonResponse(QNetworkReply* reply, QJsonDocument& doc, QString& error);

    QNetworkAccessManager m_net;
    int m_currentRequestId = 0;
    QString m_currentSlug;

    static constexpr int REQUEST_TIMEOUT_MS = 8000;
};