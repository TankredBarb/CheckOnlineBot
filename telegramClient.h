#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>

struct TgMessage
{
    qint64 chatId = 0;
    QString text;
    qint64 messageThreadId = 0; // 0 for DMs/Groups, specific ID for Forum topics
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
    void pollUpdates();
    void handleUpdatesReply();

private:
    QString m_token;
    QNetworkAccessManager m_net;
    QTimer m_pollTimer;
    qint64 m_offset = 0;
};