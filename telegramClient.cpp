#include "telegramClient.h"
#include "config.h"
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>

TelegramClient::TelegramClient(const QString& token, QObject* parent)
    : QObject(parent)
    , m_token(token)
{
}

void TelegramClient::startPolling()
{
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]
    {
        QMap<QString, QString> params;
        params["offset"] = QString::number(m_offset);
        params["timeout"] = "30";
        sendRequest("getUpdates", params);
    });
    timer->start(1000);
    qDebug() << "[TelegramClient] Polling started.";
}

void TelegramClient::sendMessage(qint64 chatId, const QString& text, qint64 messageThreadId, const QJsonObject& replyMarkup)
{
    QMap<QString, QString> params;
    params["chat_id"] = QString::number(chatId);
    params["text"] = text;
    params["parse_mode"] = "HTML";

    if (messageThreadId > 0)
    {
        params["message_thread_id"] = QString::number(messageThreadId);
    }

    if (!replyMarkup.isEmpty())
    {
        QJsonDocument doc(replyMarkup);
        params["reply_markup"] = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }

    sendRequest("sendMessage", params);
}

void TelegramClient::answerCallbackQuery(const QString& callbackQueryId, const QString& text)
{
    QMap<QString, QString> params;
    params["callback_query_id"] = callbackQueryId;
    if (!text.isEmpty())
    {
        params["text"] = text;
        params["show_alert"] = "false";
    }
    sendRequest("answerCallbackQuery", params);
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

        msg.chatId = message["chat"].toObject()["id"].toVariant().toLongLong();
        msg.messageThreadId = message.contains("message_thread_id") ? message["message_thread_id"].toInt() : 0;

        if (message.contains("text"))
        {
            msg.text = message["text"].toString();
            emit messageReceived(msg);
        }
    }
    else if (update.contains("callback_query"))
    {
        QJsonObject callback = update["callback_query"].toObject();
        QString callbackQueryId = callback["id"].toString();
        QString callbackData = callback["data"].toString();
        QJsonObject message = callback["message"].toObject();
        QJsonObject chat = message["chat"].toObject();
        qint64 chatId = chat["id"].toVariant().toLongLong();
        qint64 topicId = message.contains("message_thread_id") ? message["message_thread_id"].toInt() : 0;
        emit callbackQueryReceived(callbackQueryId, callbackData, chatId, topicId);
    }
}

void TelegramClient::sendPhoto(qint64 chatId, const QByteArray& pngData, const QString& caption, qint64 topicId)
{
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart chatIdPart;
    chatIdPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"chat_id\""));
    chatIdPart.setBody(QString::number(chatId).toUtf8());
    multiPart->append(chatIdPart);

    if (topicId != 0)
    {
        QHttpPart topicPart;
        topicPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"message_thread_id\""));
        topicPart.setBody(QString::number(topicId).toUtf8());
        multiPart->append(topicPart);
    }

    QHttpPart captionPart;
    captionPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"caption\""));
    captionPart.setBody(caption.toUtf8());
    multiPart->append(captionPart);

    QHttpPart parseModePart;
    parseModePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"parse_mode\""));
    parseModePart.setBody("HTML");
    multiPart->append(parseModePart);

    QHttpPart photoPart;
    photoPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"photo\"; filename=\"chart.png\""));
    photoPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    photoPart.setBody(pngData);
    multiPart->append(photoPart);

    QUrl url(QString("https://api.telegram.org/bot%1/sendPhoto").arg(Config::TG_TOKEN));
    QNetworkRequest request(url);

    QNetworkReply* reply = m_net.post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, [reply]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "[TelegramClient] sendPhoto error:" << reply->errorString();
        reply->deleteLater();
    });
}