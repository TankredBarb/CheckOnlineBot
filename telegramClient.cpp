#include "telegramClient.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QTimer>

TelegramClient::TelegramClient(const QString& token, QObject* parent)
    : QObject(parent), m_token(token)
{
}

void TelegramClient::startPolling()
{
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, [this]()
    {
        QMap<QString, QString> params;
        params["offset"] = QString::number(m_offset);
        params["timeout"] = "30";
        sendRequest("getUpdates", params);
    });
    timer->start(1000);
    qDebug() << "[TelegramClient] Polling started.";
}

void TelegramClient::sendMessage(qint64 chatId, const QString& text, qint64 messageThreadId)
{
    QMap<QString, QString> params;
    params["chat_id"] = QString::number(chatId);
    params["text"] = text;
    params["parse_mode"] = "HTML";  // Using HTML formatting

    if (messageThreadId > 0)
    {
        params["message_thread_id"] = QString::number(messageThreadId);
    }

    sendRequest("sendMessage", params);
}

void TelegramClient::sendRequest(const QString& method, const QMap<QString, QString>& params)
{
    QUrl url("https://api.telegram.org/bot" + m_token + "/" + method);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject jsonObj;
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        jsonObj[it.key()] = it.value();
    }

    QJsonDocument doc(jsonObj);
    QNetworkReply* reply = m_net.post(request, doc.toJson(QJsonDocument::Compact));

    if (method == "getUpdates")
        connect(reply, &QNetworkReply::finished, this, &TelegramClient::onPollReplyFinished);
    else
        connect(reply, &QNetworkReply::finished, this, &TelegramClient::onSendReplyFinished);
}

void TelegramClient::onPollReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    if (reply->error() == QNetworkReply::NoError)
    {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject() && doc.object()["ok"].toBool())
        {
            for (const QJsonValue& val : doc.object()["result"].toArray())
            {
                processUpdate(val.toObject());
            }
        }
    }
    reply->deleteLater();
}

void TelegramClient::onSendReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "[TG] Send error:" << reply->errorString();
    }
    else
    {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.object()["ok"].toBool())
        {
            qWarning() << "[TG] API error:" << doc.object()["description"].toString();
        }
    }
    reply->deleteLater();
}

void TelegramClient::processUpdate(const QJsonObject& update)
{
    if (update.contains("update_id"))
        m_offset = update["update_id"].toInt() + 1;

    if (update.contains("message"))
    {
        QJsonObject message = update["message"].toObject();
        TgMessage msg;

        // Explicit cast to QJsonObject before accessing field
        msg.chatId = message["chat"].toObject()["id"].toVariant().toLongLong();
        msg.messageThreadId = message.contains("message_thread_id") ? message["message_thread_id"].toInt() : 0;

        if (message.contains("text"))
        {
            msg.text = message["text"].toString();
            emit messageReceived(msg);
        }
    }
}