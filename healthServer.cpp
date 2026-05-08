#include "healthServer.h"
#include <QTcpSocket>
#include <QDateTime>
#include <QDebug>
#include <QTimer>

HealthServer::HealthServer(QObject *parent) : QTcpServer(parent)
{
}

void HealthServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor))
    {
        qWarning() << "[Health] Could not set socket descriptor:" << socket->errorString();
        delete socket;
        return;
    }

    QString peerAddress = socket->peerAddress().toString();
    
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, peerAddress]() {
        // Read incoming request (basic log)
        QByteArray request = socket->readAll();
        if (request.contains("GET"))
        {
            qDebug() << QString("[%1] [Health] GET request from %2")
                        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                        .arg(peerAddress);
        }

        // Standard HTTP 200 OK response
        socket->write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/plain\r\n"
                      "Content-Length: 2\r\n"
                      "Server: CheckOnlineBot-Health\r\n"
                      "Connection: close\r\n"
                      "\r\n"
                      "OK");
        
        socket->disconnectFromHost();
    });

    // Auto-delete socket when done
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    
    // Timeout protection: if no data read within 5s, close connection
    QTimer::singleShot(5000, socket, [socket]() {
        if (socket->state() == QAbstractSocket::ConnectedState)
        {
            socket->disconnectFromHost();
        }
    });
}
