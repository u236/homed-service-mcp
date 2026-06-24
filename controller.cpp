#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QUuid>
#include "controller.h"
#include "expose.h"
#include "logger.h"
#include "parser.h"


// not reviewed
static QJsonArray loadJsonArray(const QString &path)
{
    QFile file(path);

    if (!file.open(QFile::ReadOnly))
    {
        logWarning << "File" << path.toUtf8().constData() << "open error:" << file.errorString().toUtf8().constData();
        return QJsonArray();
    }

    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);

    file.close();

    if (error.error != QJsonParseError::NoError || !document.isArray())
    {
        logWarning << "File" << path.toUtf8().constData() << "parse error:" << error.errorString().toUtf8().constData();
        return QJsonArray();
    }

    return document.array();
}

// not reviewed
Controller::Controller(const QString &configFile) : HOMEd(SERVICE_VERSION, configFile), m_tcpServer(new QTcpServer(this)), m_timer(new QTimer(this))
{
    m_token = getConfig()->value("server/token").toString();
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_readOnly = getConfig()->value("server/readOnly", true).toBool();
    m_debug = getConfig()->value("server/debug", false).toBool();

    m_tools = loadJsonArray(getConfig()->value("server/tools", "/usr/share/homed-mcp/tools.json").toString());
    m_resources = loadJsonArray(getConfig()->value("server/resources", "/usr/share/homed-mcp/resources.json").toString());

    connect(m_tcpServer, &QTcpServer::newConnection, this, &Controller::socketConnected);
    connect(m_timer, &QTimer::timeout, this, &Controller::checkPending);

    if (m_token.isEmpty())
        logWarning << "Authentication token is empty, server is open!";

    if (!m_tcpServer->listen(QHostAddress(getConfig()->value("server/address", "0.0.0.0").toString()), static_cast <quint16> (getConfig()->value("server/port", 8086).toInt())))
        logWarning << "TCP server listen failed:" << m_tcpServer->errorString();
    else
        logInfo << "MCP server listening on" << m_tcpServer->serverAddress().toString().append(':').append(QString::number(m_tcpServer->serverPort())).toUtf8().constData() << (m_readOnly ? "(read-only)" : "(read-write)");

    m_timer->start(1000);
}





Device Controller::findDevice(const QString &search)
{
    for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        if (search == it.value()->key() || search.startsWith(it.value()->key().append('/')) || search == it.value()->topic() || search.startsWith(it.value()->topic().append('/')) || search.toLower() == it.value()->name().toLower())
            return it.value();

    return Device();
}





// not reviewed
void Controller::httpResponse(QTcpSocket *socket, quint16 code, const QByteArray &body, const QString &contentType)
{
    QByteArray data;

    switch (code)
    {
        case 200: data = "HTTP/1.1 200 OK"; break;
        case 202: data = "HTTP/1.1 202 Accepted"; break;
        case 400: data = "HTTP/1.1 400 Bad Request"; break;
        case 401: data = "HTTP/1.1 401 Unauthorized"; break;
        case 404: data = "HTTP/1.1 404 Not Found"; break;
        case 405: data = "HTTP/1.1 405 Method Not Allowed"; break;
        case 500: data = "HTTP/1.1 500 Internal Server Error"; break;
        default:  data = QString("HTTP/1.1 %1").arg(code).toUtf8(); break;
    }

    data.append("\r\nAccess-Control-Allow-Origin: *");
    data.append("\r\nAccess-Control-Allow-Methods: POST, OPTIONS");
    data.append("\r\nAccess-Control-Allow-Headers: Content-Type, Authorization, Mcp-Session-Id");
    data.append("\r\nAccess-Control-Expose-Headers: Mcp-Session-Id");
    data.append(QString("\r\nMcp-Session-Id: %1").arg(m_sessionId).toUtf8());

    if (!body.isEmpty())
    {
        data.append(QString("\r\nContent-Type: %1").arg(contentType).toUtf8());
        data.append(QString("\r\nContent-Length: %1").arg(body.length()).toUtf8());
    }
    else
        data.append("\r\nContent-Length: 0");

    socket->write(data.append("\r\nConnection: close\r\n\r\n").append(body));
    socket->flush();
    socket->disconnectFromHost();
}

// not reviewed
void Controller::rpcResponse(QTcpSocket *socket, const QJsonValue &id, const QJsonValue &result)
{
    QJsonObject response = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    httpResponse(socket, 200, QJsonDocument(response).toJson(QJsonDocument::Compact));
}

// not reviewed
void Controller::rpcError(QTcpSocket *socket, const QJsonValue &id, int code, const QString &message)
{
    QJsonObject response = {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject {{"code", code}, {"message", message}}}};
    httpResponse(socket, 200, QJsonDocument(response).toJson(QJsonDocument::Compact));
}

// not reviewed
QJsonObject Controller::toolResult(const QString &text, bool isError)
{
    return QJsonObject {{"content", QJsonArray {QJsonObject {{"type", "text"}, {"text", text}}}}, {"isError", isError}};
}

// not reviewed
QJsonArray Controller::propertyNames(const Device &device)
{
    QJsonArray result;
    QString prefix = device->key().append('/');

    for (auto it = m_propertyNames.begin(); it != m_propertyNames.end(); it++)
    {
        if (!it.key().startsWith(prefix))
            continue;

        QString rest = it.key().mid(prefix.length());
        QStringList parts = rest.split('/');
        QJsonObject entry;
        entry.insert("display_name", it.value().toString());

        if (parts.size() > 1)
        {
            entry.insert("endpoint", parts.value(0).toInt());
            entry.insert("property", parts.value(1));
        }
        else
            entry.insert("property", parts.value(0));

        result.append(entry);
    }

    return result;
}





QJsonObject Controller::deviceInfo(const Device &device)
{
    QJsonArray names = propertyNames(device);
    QJsonObject json = {{"key", device->key()}, {"type", device->type()}, {"service", device->service()}, {"name", device->name()}, {"available", device->available()}}, properties = Expose::serialize(device);

    if (!names.isEmpty())
        json.insert("named_properties", names);

    if (!properties.isEmpty())
        json.insert("properties", properties);

    return json;
}

void Controller::mqttConnected(void)
{
    mqttSubscribe(mqttTopic("command/mcp"));
    mqttSubscribe(mqttTopic("recorder"));
    mqttSubscribe(mqttTopic("service/#"));

    m_services.clear();
    m_devices.clear();

    mqttPublishService();
}

void Controller::mqttReceived(const QByteArray &message, const QMqttTopicName &topic)
{
    QString subTopic = topic.name().replace(0, mqttTopic().length(), QString());
    QJsonObject json = QJsonDocument::fromJson(message).object();

    if (subTopic == "command/mcp")
    {
        if (json.value("action").toString() != "restartService")
            return;

        logWarning << "Restart request received...";
        mqttPublish(topic.name(), QJsonObject(), true);
        QCoreApplication::exit(EXIT_RESTART);
    }
    else if (subTopic == "recorder")
    {
        completeHistoryRequest(json.value("id").toString(), json);
        // TODO: move completeHistoryRequest code here
    }
    else if (subTopic.startsWith("service/"))
    {
        QString type = subTopic.split('/').value(1), service = subTopic.mid(subTopic.indexOf('/') + 1);

        if (coreServices().contains(type) && type != "recorder" && type != "web")
            return;

        if (json.value("status").toString() == "online")
        {
            mqttSubscribe(mqttTopic("status/%1").arg(service));

            if (!m_services.contains(service))
                m_services.append(service);

            return;
        }

        for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        {
            const Device &device = it.value();

            if (!device->topic().startsWith(QString("%1/").arg(service)))
                continue;

            mqttUnsubscribe(mqttTopic("device/%1").arg(it.value()->topic()));
            mqttUnsubscribe(mqttTopic("expose/%1").arg(it.value()->topic()));
            mqttUnsubscribe(mqttTopic("fd/%1").arg(it.value()->topic()));
            mqttUnsubscribe(mqttTopic("fd/%1/#").arg(it.value()->topic()));
            device->clearTopic();
        }

        mqttUnsubscribe(mqttTopic("status/%1").arg(service));
        m_services.removeAll(service);
    }

    else if (subTopic.startsWith("status/"))
    {
        QString type = subTopic.split('/').value(1), service = subTopic.mid(subTopic.indexOf('/') + 1);
        QJsonArray devices = json.value("devices").toArray();
        bool names = json.value("names").toBool();

        if (type == "web")
        {
            QJsonObject items = json.value("names").toObject();

            for (auto it = items.begin(); it != items.end(); it++)
                m_propertyNames.insert(it.key(), it.value());

            return;
        }

        if (coreServices().contains(type))
            return;

        for (auto it = devices.begin(); it != devices.end(); it++)
        {
            QJsonObject item = it->toObject();
            QString name = item.value("name").toString(), id = deviceId(item, type), key, topic;
            bool check = false;

            if (type == "zigbee" && (item.value("removed").toBool() || !item.value("logicalType").toInt()))
                continue;

            if (name.isEmpty())
                name = id;

            key = QString("%1/%2").arg(type, id);
            topic = QString("%1/%2").arg(service, names ? name : id);

            if (m_devices.contains(key))
            {
                const Device &device = m_devices.value(key);

                if (device->topic() != topic)
                {
                    if (!device->topic().isEmpty())
                    {
                        mqttUnsubscribe(mqttTopic("device/%1").arg(device->topic()));
                        mqttUnsubscribe(mqttTopic("expose/%1").arg(device->topic()));
                        mqttUnsubscribe(mqttTopic("fd/%1").arg(device->topic()));
                        mqttUnsubscribe(mqttTopic("fd/%1/#").arg(device->topic()));
                    }

                    device->setTopic(topic);
                    check = true;
                }

                device->setName(name);
            }
            else
            {
                m_devices.insert(key, Device(new DeviceObject(key, topic, name)));
                check = true;
            }

            if (check)
            {
                mqttSubscribe(mqttTopic("device/%1").arg(topic));
                mqttSubscribe(mqttTopic("expose/%1").arg(topic));
            }
        }

        return;
    }
    else if (subTopic.startsWith("device/"))
    {
        const Device &device = findDevice(subTopic.mid(subTopic.indexOf('/') + 1));

        if (device.isNull())
            return;

        device->setAvailable(json.value("status").toString() == "online");
    }
    else if (subTopic.startsWith("expose/"))
    {
        const Device &device = findDevice(subTopic.mid(subTopic.indexOf('/') + 1));
        QByteArray hash = QCryptographicHash::hash(message, QCryptographicHash::Md5);

        if (device.isNull() || device->hash() == hash)
            return;

        Expose::parse(device, json); // TODO: refactor this
        device->setHash(hash);

        mqttSubscribe(mqttTopic("fd/%1").arg(device->topic()));
        mqttSubscribe(mqttTopic("fd/%1/#").arg(device->topic()));
        mqttPublish(mqttTopic("command/%1").arg(device->service()), {{"action", "getProperties"}, {"device", device->topic().mid(device->service().length() + 1)}, {"service", "mcp"}});
    }
    else if (subTopic.startsWith("fd/"))
    {
        QString string = subTopic.mid(subTopic.indexOf('/') + 1);
        const Device &device = findDevice(string);

        if (!device.isNull())
        {
            QList <QString> endpointList = string.split('/'), propertyList = {"action", "event", "scene"};
            const Endpoint endpoint = device->endpoints().value(endpointList.count() > 2 ? static_cast <quint8> (endpointList.last().toInt()) : 0);

            if (endpoint.isNull())
                return;

            for (auto it = json.begin(); it != json.end(); it++)
            {
                if (!endpoint->properties().contains(it.key()) || propertyList.contains(it.key().split('_').value(0)))
                    continue;

                endpoint->properties().value(it.key())->setValue(it.value().toVariant());
            }
        }
    }
}

void Controller::socketConnected(void)
{
    QTcpSocket *socket = m_tcpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::disconnected, this, &Controller::socketDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &Controller::readyRead);
    m_sockets.append(socket);
}

void Controller::socketDisconnected(void)
{
    QTcpSocket *socket = reinterpret_cast <QTcpSocket*> (sender());

    for (auto it = m_pending.begin(); it != m_pending.end(); NULL)
    {
        if (it->socket == socket)
        {
            it = m_pending.erase(it);
            continue;
        }

        it++;
    }

    m_sockets.removeAll(socket);
    socket->deleteLater();
}





// not reviewed
void Controller::readyRead(void)
{
    QTcpSocket *socket = reinterpret_cast <QTcpSocket*> (sender());
    QByteArray request = socket->peek(socket->bytesAvailable());
    int separator = request.indexOf("\r\n\r\n");

    if (separator < 0)
        return;

    QList <QByteArray> head = request.left(separator).split('\n');
    QByteArray body = request.mid(separator + 4);
    QList <QByteArray> target = head.value(0).trimmed().split(0x20);
    QString method = target.value(0), url = target.value(1);
    QMap <QString, QString> headers;

    for (int i = 1; i < head.count(); i++)
    {
        QByteArray line = head.at(i).trimmed();
        int colon = line.indexOf(':');

        if (colon < 0)
            continue;

        headers.insert(QString(line.left(colon)).toLower(), QString(line.mid(colon + 1)).trimmed());
    }

    int contentLength = headers.value("content-length").toInt();

    if (contentLength > body.length())
    {
        if (!socket->waitForReadyRead(2000))
        {
            disconnect(socket, &QTcpSocket::readyRead, this, &Controller::readyRead);
            httpResponse(socket, 400);
            return;
        }

        request = socket->peek(socket->bytesAvailable());
        body = request.mid(separator + 4);
    }

    if (contentLength > body.length())
        return;

    disconnect(socket, &QTcpSocket::readyRead, this, &Controller::readyRead);
    logDebug(m_debug) << "Request" << method.toUtf8().constData() << url.toUtf8().constData() << "received from" << socket->peerAddress().toString();

    if (method == "OPTIONS")
    {
        httpResponse(socket, 200);
        return;
    }

    if (method != "POST")
    {
        httpResponse(socket, 405);
        return;
    }

    Q_UNUSED(url)

    if (!m_token.isEmpty())
    {
        QString authorization = headers.value("authorization");

        if (!authorization.startsWith("Bearer ") || authorization.mid(7) != m_token)
        {
            httpResponse(socket, 401);
            return;
        }
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(body, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        rpcError(socket, QJsonValue::Null, -32700, "Parse error");
        return;
    }

    handleRpc(socket, document.object());
}

// not reviewed
void Controller::handleRpc(QTcpSocket *socket, const QJsonObject &request)
{
    QString rpcMethod = request.value("method").toString();
    QJsonValue rpcId = request.value("id");
    QJsonObject params = request.value("params").toObject();
    bool isNotification = !request.contains("id");

    logDebug(m_debug) << "RPC method" << rpcMethod.toUtf8().constData();

    if (isNotification)
    {
        httpResponse(socket, 202);
        return;
    }

    if (rpcMethod == "initialize")
    {
        rpcResponse(socket, rpcId, QJsonObject {
            {"protocolVersion", MCP_PROTOCOL_VERSION},
            {"capabilities", QJsonObject {
                {"tools", QJsonObject {{"listChanged", false}}},
                {"resources", QJsonObject {{"listChanged", false}, {"subscribe", false}}}
            }},
            {"serverInfo", QJsonObject {
                {"name", "homed-mcp"},
                {"version", SERVICE_VERSION}
            }},
            {"instructions", "Access HOMEd smart-home devices and historical data over MQTT."}
        });
        return;
    }

    if (rpcMethod == "ping")
    {
        rpcResponse(socket, rpcId, QJsonObject());
        return;
    }

    if (rpcMethod == "tools/list")
    {
        rpcResponse(socket, rpcId, QJsonObject {{"tools", toolsList()}});
        return;
    }

    if (rpcMethod == "tools/call")
    {
        handleToolsCall(socket, rpcId, params.value("name").toString(), params.value("arguments").toObject());
        return;
    }

    if (rpcMethod == "resources/list")
    {
        rpcResponse(socket, rpcId, QJsonObject {{"resources", resourcesList()}});
        return;
    }

    if (rpcMethod == "resources/read")
    {
        handleResourcesRead(socket, rpcId, params.value("uri").toString());
        return;
    }

    rpcError(socket, rpcId, -32601, QString("Method not found: %1").arg(rpcMethod));
}

// not reviewed
QJsonArray Controller::toolsList(void)
{
    QJsonArray result;

    for (auto it = m_tools.begin(); it != m_tools.end(); it++)
    {
        QJsonObject tool = it->toObject();

        if (m_readOnly && tool.value("requiresWrite").toBool())
            continue;

        tool.remove("requiresWrite");
        result.append(tool);
    }

    return result;
}

// not reviewed
QJsonArray Controller::resourcesList(void)
{
    return m_resources;
}

// not reviewed
void Controller::handleToolsCall(QTcpSocket *socket, const QJsonValue &rpcId, const QString &name, const QJsonObject &arguments)
{
    logInfo << "tools/call" << name.toUtf8().constData() << QJsonDocument(arguments).toJson(QJsonDocument::Compact).constData();

    if (name == "list_devices")
    {
        QString service = arguments.value("service").toString(), type = arguments.value("type").toString();
        QJsonArray result;

        for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        {
            if (!service.isEmpty() && it.value()->service() != service)
                continue;

            if (!type.isEmpty() && it.value()->type() != type)
                continue;

            result.append(deviceInfo(it.value()));
        }

        rpcResponse(socket, rpcId, toolResult(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        return;
    }

    if (name == "get_device")
    {
        Device device = findDevice(arguments.value("device").toString());

        if (device.isNull())
        {
            rpcResponse(socket, rpcId, toolResult(QString("Device \"%1\" not found").arg(arguments.value("device").toString()), true));
            return;
        }

        rpcResponse(socket, rpcId, toolResult(QJsonDocument(deviceInfo(device)).toJson(QJsonDocument::Compact)));
        return;
    }

    if (name == "set_properties")
    {
        if (m_readOnly)
        {
            rpcResponse(socket, rpcId, toolResult("Service is running in read-only mode", true));
            return;
        }

        QJsonArray operations = arguments.value("operations").toArray();
        QJsonArray results;
        int ok = 0, failed = 0;

        for (auto it = operations.begin(); it != operations.end(); it++)
        {
            QJsonObject op = it->toObject();
            QString deviceArg = op.value("device").toString();
            int endpoint = op.value("endpoint").toInt();
            QJsonObject properties = op.value("properties").toObject();
            Device device = findDevice(deviceArg);

            if (device.isNull())
            {
                results.append(QJsonObject {{"device", deviceArg}, {"ok", false}, {"error", "device not found"}});
                failed++;
                continue;
            }

            if (properties.isEmpty())
            {
                results.append(QJsonObject {{"device", device->key()}, {"ok", false}, {"error", "properties is empty"}});
                failed++;
                continue;
            }

            const Endpoint &target = device->endpoints().value(static_cast <quint8> (endpoint));
            QStringList writableSubKeys, invalidSubKeys;

            if (!target.isNull())
                for (auto e = target->properties().begin(); e != target->properties().end(); e++)
                    if (e.value()->writable())
                        writableSubKeys.append(e.key());

            for (auto jt = properties.begin(); jt != properties.end(); jt++)
                if (!writableSubKeys.contains(jt.key()))
                    invalidSubKeys.append(jt.key());

            if (!invalidSubKeys.isEmpty() && !writableSubKeys.isEmpty())
            {
                results.append(QJsonObject {
                    {"device", device->key()},
                    {"endpoint", endpoint ? QJsonValue(endpoint) : QJsonValue()},
                    {"ok", false},
                    {"error", QString("unknown sub-key(s): %1; writable sub-keys for this (device, endpoint) are: %2").arg(invalidSubKeys.join(", "), writableSubKeys.join(", "))}
                });
                failed++;
                continue;
            }

            QString topic = endpoint ? QString("td/%1/%2").arg(device->topic()).arg(endpoint) : QString("td/%1").arg(device->topic());
            QJsonObject payload;

            for (auto jt = properties.begin(); jt != properties.end(); jt++)
            {
                QJsonValue value = jt.value();
                payload.insert(jt.key(), value.isString() ? QJsonValue::fromVariant(Parser::stringValue(value.toString())) : value);
            }

            mqttPublish(mqttTopic(topic), payload);
            results.append(QJsonObject {{"device", device->key()}, {"topic", topic}, {"properties", QJsonArray::fromStringList(properties.keys())}, {"ok", true}});
            ok++;
        }

        logInfo << "set_properties published" << ok << "of" << operations.size() << "operations" << (failed ? QString("(%1 failed)").arg(failed).toUtf8().constData() : "");
        rpcResponse(socket, rpcId, toolResult(QJsonDocument(results).toJson(QJsonDocument::Compact), failed > 0 && ok == 0));
        return;
    }

    if (name == "query_history")
    {
        if (!m_services.contains("recorder"))
        {
            rpcResponse(socket, rpcId, toolResult("homed-recorder service is not online", true));
            return;
        }

        Device device = findDevice(arguments.value("device").toString());

        if (device.isNull())
        {
            rpcResponse(socket, rpcId, toolResult(QString("Device \"%1\" not found").arg(arguments.value("device").toString()), true));
            return;
        }

        QString property = arguments.value("property").toString();
        QString correlationId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        m_pending.append({socket, rpcId, correlationId, QDateTime::currentMSecsSinceEpoch() + MCP_REQUEST_TIMEOUT});

        mqttPublish(mqttTopic("command/recorder"), QJsonObject {
            {"action", "getData"},
            {"id", correlationId},
            {"endpoint", device->key()},
            {"property", property},
            {"start", arguments.value("start")},
            {"end", arguments.value("end")}
        });

        return;
    }

    rpcError(socket, rpcId, -32602, QString("Unknown tool: %1").arg(name));
}

// not reviewed
void Controller::handleResourcesRead(QTcpSocket *socket, const QJsonValue &rpcId, const QString &uri)
{
    QByteArray text;

    if (uri == "homed://devices")
    {
        QJsonArray array;

        for (auto it = m_devices.begin(); it != m_devices.end(); it++)
            array.append(deviceInfo(it.value()));

        text = QJsonDocument(array).toJson(QJsonDocument::Compact);
    }
    else if (uri == "homed://services")
    {
        text = QJsonDocument(QJsonArray::fromStringList(m_services)).toJson(QJsonDocument::Compact);
    }
    else
    {
        rpcError(socket, rpcId, -32602, QString("Unknown resource: %1").arg(uri));
        return;
    }

    rpcResponse(socket, rpcId, QJsonObject {{"contents", QJsonArray {QJsonObject {
        {"uri", uri},
        {"text", QString::fromUtf8(text)}
    }}}});
}

// not reviewed
void Controller::completeHistoryRequest(const QString &correlationId, const QJsonObject &payload)
{
    PendingRequest pending;
    bool found = false;

    for (int i = 0; i < m_pending.size(); i++)
    {
        if (m_pending.at(i).correlationId != correlationId)
            continue;

        pending = m_pending.at(i);
        m_pending.removeAt(i);
        found = true;
        break;
    }

    if (!found || !m_sockets.contains(pending.socket))
        return;

    QJsonObject result = payload;
    result.remove("id");
    rpcResponse(pending.socket, pending.rpcId, toolResult(QJsonDocument(result).toJson(QJsonDocument::Compact)));
}

// not reviewed
void Controller::checkPending(void)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList <PendingRequest> expired;

    for (auto it = m_pending.begin(); it != m_pending.end(); NULL)
    {
        if (it->expires <= now)
        {
            expired.append(*it);
            it = m_pending.erase(it);
            continue;
        }

        it++;
    }

    for (const PendingRequest &p : expired)
    {
        if (m_sockets.contains(p.socket))
            rpcResponse(p.socket, p.rpcId, toolResult("Request timed out", true));
    }
}
