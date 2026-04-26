#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include "config.h"

// [+] Strict platform category enumeration matching API IDs
enum class PlatformCategory : int
{
    Xbox = 1,
    PlayStation = 2,
    Steam = 3,
    Stadia = 5,
    EpicGamesStore = 6
};

// [+] Helper for UI/logging conversion
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

class PopularityApi : public QObject
{
    Q_OBJECT

public:
    explicit PopularityApi(QObject* parent = nullptr);
    void requestCrossPlatformPlayer(const QString& gameSlug, int requestId);
    void requestPlatformDistribution();

signals:
    void popularityDataReady(int players, const QString& error, QString gameSlug, int requestId);
    void platformDistributionReceived(const QMap<PlatformCategory, int>& platformStats);

private slots:
    void onPopularityReplyFinished();
    void onPlatformDistributionFinished(); // [+] Removed parameter for consistent Qt style

private:
    void setStandardHeaders(QNetworkRequest& request);

    QNetworkAccessManager m_net;
    int m_currentRequestId = 0;
    QString m_currentSlug;
};