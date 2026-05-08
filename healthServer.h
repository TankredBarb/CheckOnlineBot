#pragma once
#include <QTcpServer>

class HealthServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit HealthServer(QObject *parent = nullptr);

protected:
    void incomingConnection(qintptr socketDescriptor) override;
};
