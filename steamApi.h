#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QMap>

class SteamApi : public QObject
{
    Q_OBJECT
public:
    explicit SteamApi(QObject* parent = nullptr);
    void requestCurrentPlayers(const QList<int>& appIds);

signals:
    void playersDataReady(const QMap<int, int>& results);

private slots:
    void onSteamReplyFinished();

private:
    QNetworkAccessManager m_net;
    int m_expectedCount = 0;
    QMap<int, int> m_tempResults;
};