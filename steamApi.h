#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QMap>

class SteamApi : public QObject
{
    Q_OBJECT
public:
    explicit SteamApi(QObject* parent = nullptr);
    void requestCurrentPlayers(const QList<int>& appIds, int requestId);

signals:
    void playersDataReady(const QMap<int, int>& results, int requestId);

private slots:
    void onSteamReplyFinished();

private:
    QNetworkAccessManager m_net;
    int m_expectedCount = 0;
    QMap<int, int> m_tempResults;
    int m_currentRequestId = 0; // Stores the ID of the active batch
};