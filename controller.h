#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION         "0.0.1"

#define MCP_PROTOCOL_VERSION    "2025-06-18"
#define MCP_REQUEST_TIMEOUT     5000

#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include "device.h"
#include "homed.h"

struct PendingRequest
{
    QTcpSocket *socket;
    QJsonValue rpcId;
    QString correlationId;
    qint64 expires;
};

class Controller : public HOMEd
{
    Q_OBJECT

public:

    Controller(const QString &configFile);

private:

    QTcpServer *m_tcpServer;
    QTimer *m_timer;

    QString m_token, m_sessionId;
    bool m_readOnly, m_debug;

    QList <QTcpSocket*> m_sockets;
    QMap <QString, Device> m_devices;
    QList <QString> m_services;
    QJsonObject m_propertyNames;
    QJsonArray m_tools, m_resources;
    QList <PendingRequest> m_pending;





    Device findDevice(const QString &search);
    quint8 getEndpointId(const QString &endpoint);

    void updatePropertyNames(const Device &device);
    QJsonObject deviceInfo(const Device &device);

    void httpResponse(QTcpSocket *socket, quint16 code, const QByteArray &response = QByteArray());
    void rpcResponse(QTcpSocket *socket, const QJsonValue &id, const QJsonValue &result);
    void rpcError(QTcpSocket *socket, const QJsonValue &id, int code, const QString &message);
    QJsonObject toolResult(const QString &text, bool error = false);





    void handleRpc(QTcpSocket *socket, const QJsonObject &request);
    void handleToolsCall(QTcpSocket *socket, const QJsonValue &rpcId, const QString &name, const QJsonObject &arguments);
    void handleResourcesRead(QTcpSocket *socket, const QJsonValue &rpcId, const QString &uri);

    QJsonArray toolsList(void);
    QJsonArray resourcesList(void);

    void completeHistoryRequest(const QString &correlationId, const QJsonObject &payload);

private slots:

    void mqttConnected(void) override;
    void mqttReceived(const QByteArray &message, const QMqttTopicName &topic) override;

    void socketConnected(void);
    void socketDisconnected(void);





    void readyRead(void);

    void checkPending(void);

};

#endif
