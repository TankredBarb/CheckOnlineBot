#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QMap>

struct TgMessage
{
    qint64 chatId;
    qint64 messageThreadId;  // Для форум-топиков (может быть 0)
    QString text;
};

class TelegramClient : public QObject
{
    Q_OBJECT
public:
    explicit TelegramClient(const QString& token, QObject* parent = nullptr);
    void startPolling();
    void sendMessage(qint64 chatId, const QString& text, qint64 messageThreadId = 0);

signals:
    void messageReceived(const TgMessage& msg);

private slots:
    void onPollReplyFinished();
    void onSendReplyFinished();

private:
    void sendRequest(const QString& method, const QMap<QString, QString>& params);
    void processUpdate(const QJsonObject& update);

    QNetworkAccessManager m_net;
    QString m_token;
    qint64 m_offset = 0;
};