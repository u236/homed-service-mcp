#ifndef CONTROLLER_H
#define CONTROLLER_H

#define SERVICE_VERSION         "0.0.1"

#define MCP_PROTOCOL_VERSION    "2025-06-18"
#define MCP_REQUEST_TIMEOUT     5000

#include <QTcpServer>
#include <QTcpSocket>
#include "homed.h"

class DeviceObject;
typedef QSharedPointer <DeviceObject> Device;

class DeviceObject
{

public:

    DeviceObject(const QString &key, const QString &topic, const QString &name) :
        m_key(key), m_topic(topic), m_name(name), m_available(false) {}

    inline QString key(void) { return m_key; }
    inline QString type(void) { return m_key.left(m_key.indexOf('/')); }
    inline QString id(void) { return m_key.mid(m_key.indexOf('/') + 1); }
    inline QString service(void) { return m_topic.left(m_topic.lastIndexOf('/')); }

    inline QString name(void) { return m_name; }
    inline void setName(const QString &value) { m_name = value; }

    inline QString topic(void) { return m_topic; }
    inline void setTopic(const QString &value) { m_topic = value; }
    inline void clearTopic(void) { m_topic.clear(); }

    inline bool available(void) { return m_available; }
    inline void setAvailable(bool value) { m_available = value; }

    inline QJsonObject exposes(void) { return m_exposes; }
    inline void setExposes(const QJsonObject &value) { m_exposes = value; }

    inline QMap <quint8, QVariantMap> &properties(void) { return m_properties; }

private:

    QString m_key, m_topic, m_name;
    bool m_available;

    QJsonObject m_exposes;
    QMap <quint8, QVariantMap> m_properties;

};

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

    QString m_token;
    bool m_readOnly, m_debug;

    QList <QTcpSocket*> m_sockets;
    QMap <QString, Device> m_devices;
    QList <QString> m_services;
    QJsonObject m_propertyNames;
    QList <PendingRequest> m_pending;

    void httpResponse(QTcpSocket *socket, quint16 code, const QByteArray &body = QByteArray(), const QString &contentType = "application/json");
    void rpcResponse(QTcpSocket *socket, const QJsonValue &id, const QJsonValue &result);
    void rpcError(QTcpSocket *socket, const QJsonValue &id, int code, const QString &message);

    QJsonObject toolResult(const QString &text, bool isError = false);
    QJsonObject describeDevice(const Device &device, bool full);
    QJsonArray devicePropertyNames(const Device &device);

    Device findDevice(const QString &search);

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
