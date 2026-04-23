#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QString>

class PopularityApi : public QObject
{
    Q_OBJECT
public:
    explicit PopularityApi(QObject* parent = nullptr);
    void requestCrossPlatformPlayer(const QString& gameSlug, int requestId);

signals:
    void popularityDataReady(int players, const QString& error, QString gameSlug, int requestId);

private slots:
    void onPopularityReplyFinished();

private:
    QNetworkAccessManager m_net;
    int m_currentRequestId = 0;
    QString m_currentSlug;
};