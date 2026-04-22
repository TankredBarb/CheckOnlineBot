#include "telegramClient.h"
#include "config.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDebug>

TelegramClient::TelegramClient(const QString& token, QObject* parent)
    : QObject(parent), m_token(token)
{
    connect(&m_pollTimer, &QTimer::timeout, this, &TelegramClient::pollUpdates);
}

void TelegramClient::startPolling()
{
    pollUpdates();
    m_pollTimer.start(Config::POLL_INTERVAL_MS);
}

void TelegramClient::pollUpdates()
{
    QString baseUrl = QString("https://api.telegram.org/bot%1/getUpdates").arg(m_token);

    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("offset", QString::number(m_offset));
    query.addQueryItem("timeout", "1");
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply* reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, &TelegramClient::handleUpdatesReply);
}

void TelegramClient::handleUpdatesReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
    {
        return;
    }

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject())
    {
        return;
    }

    QJsonObject root = doc.object();
    if (!root.contains("result"))
    {
        return;
    }

    QJsonArray results = root["result"].toArray();

    for (const QJsonValue& val : results)
    {
        QJsonObject obj = val.toObject();

        qint64 updateId = obj["update_id"].toVariant().toLongLong();
        if (updateId >= m_offset)
        {
            m_offset = updateId + 1;
        }

        if (obj.contains("message"))
        {
            QJsonObject msgObj = obj["message"].toObject();

            TgMessage tm;
            if (msgObj.contains("chat"))
            {
                QJsonObject chatObj = msgObj["chat"].toObject();
                tm.chatId = chatObj["id"].toVariant().toLongLong();

                // Handle Forum Topic ID logic
                if (msgObj.contains("message_thread_id"))
                {
                    // Explicit topic ID provided
                    tm.messageThreadId = msgObj["message_thread_id"].toVariant().toLongLong();
                }
                else if (chatObj.value("is_forum").toBool())
                {
                    // It's a forum, but no thread_id means it's the "General" topic (ID 1)
                    tm.messageThreadId = 1;
                }
                // Else: regular DM or Group, threadId stays 0
            }

            if (msgObj.contains("text"))
            {
                tm.text = msgObj["text"].toString();
            }

            if (!tm.text.isEmpty() && tm.chatId != 0)
            {
                emit messageReceived(tm);
            }
        }
    }
}


void TelegramClient::sendMessage(qint64 chatId, const QString& text, qint64 messageThreadId)
{
    QString baseUrl = QString("https://api.telegram.org/bot%1/sendMessage").arg(m_token);

    QUrl url(baseUrl);
    QUrlQuery query;
    query.addQueryItem("chat_id", QString::number(chatId));
    query.addQueryItem("text", text);
    query.addQueryItem("parse_mode", "HTML");

    // FIX: Telegram API often rejects explicit message_thread_id=1 for the "General" topic.
    // We only add the parameter if the ID is greater than 1.
    if (messageThreadId > 1)
    {
        query.addQueryItem("message_thread_id", QString::number(messageThreadId));
    }

    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply* reply = m_net.post(request, QByteArray());

    // Debug logging for network errors
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[TelegramClient] Send Error:" << reply->errorString();
            // Uncomment below to see full server response for debugging
            // qWarning() << "[TelegramClient] Response Body:" << reply->readAll();
        } else {
            qDebug() << "[TelegramClient] Message sent successfully.";
        }
        reply->deleteLater();
    });
}