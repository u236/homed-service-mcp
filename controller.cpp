#include <QCryptographicHash>
#include "controller.h"
#include "logger.h"
#include "parser.h"

Controller::Controller(const QString &configFile) : HOMEd(SERVICE_VERSION, configFile), m_tcpServer(new QTcpServer(this)), m_timer(new QTimer(this))
{
    QFile initialize(getConfig()->value("server/initialize", basePath().append("share/homed-mcp/initialize.json")).toString()), resources(getConfig()->value("server/resources", basePath().append("share/homed-mcp/resources.json")).toString()), tools(getConfig()->value("server/tools", basePath().append("share/homed-mcp/tools.json")).toString());

    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_token = getConfig()->value("server/token").toString();
    m_write = getConfig()->value("server/write", false).toBool();
    m_debug = getConfig()->value("server/debug", false).toBool();

    if (initialize.open(QFile::ReadOnly))
    {
        m_initialize = QJsonDocument::fromJson(QString(initialize.readAll()).arg(SERVICE_VERSION, PROTOCOL_VERSION).toUtf8()).object();
        initialize.close();
    }

    if (resources.open(QFile::ReadOnly))
    {
        m_resources = QJsonDocument::fromJson(resources.readAll()).array();
        resources.close();
    }

    if (tools.open(QFile::ReadOnly))
    {
        m_tools = QJsonDocument::fromJson(tools.readAll()).array();
        tools.close();
    }

    connect(m_tcpServer, &QTcpServer::newConnection, this, &Controller::socketConnected);
    connect(m_timer, &QTimer::timeout, this, &Controller::checkRequests);

    if (!m_tcpServer->listen(QHostAddress(getConfig()->value("server/address", "0.0.0.0").toString()), static_cast <quint16> (getConfig()->value("server/port", 8086).toInt())))
    {
        logWarning << "Failed to start server, error:" << m_tcpServer->errorString();
        return;
    }

    if (m_token.isEmpty())
        logWarning << "Authentication token is empty, server is open";

    m_timer->start(1000);

    if (m_write)
        return;

    for (auto it = m_tools.begin(); it != m_tools.end(); it++)
    {
        if (it->toObject().value("name").toString() != "set_properties")
            continue;

        m_tools.erase(it);
        break;
    }
}

Device Controller::findDevice(const QString &search)
{
    for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        if (search == it.value()->key() || search.startsWith(it.value()->key().append('/')) || search == it.value()->topic() || search.startsWith(it.value()->topic().append('/')) || search.toLower() == it.value()->name().toLower())
            return it.value();

    return Device();
}

quint8 Controller::getEndpointId(const QString &endpoint)
{
    QList <QString> list = endpoint.split('/');

    if (list.count() > 2)
        return static_cast <quint8> (list.last().toInt());

    return 0;
}

quint8 Controller::getEndpointId(const QVariant &value)
{
    return value.toString() != "common" ? static_cast <quint8> (value.toInt()) : 0;
}

void Controller::updateProperties(const Device &device)
{
    QString key = device->key().append('/');

    for (auto it = m_recordedItems.begin(); it != m_recordedItems.end(); it++)
    {
        if (it->startsWith(key))
        {
            const Endpoint &endpoint = device->endpoints().value(getEndpointId(it->mid(0, it->lastIndexOf('/'))));
            QString property = it->split('/').last();

            if (endpoint.isNull() || !endpoint->properties().contains(property))
                continue;

            endpoint->properties().value(property)->setHistory(true);
        }
    }

    for (auto it = m_propertyNames.begin(); it != m_propertyNames.end(); it++)
    {
        if (it.key().startsWith(key))
        {
            const Endpoint &endpoint = device->endpoints().value(getEndpointId(it.key().mid(0, it.key().lastIndexOf('/'))));
            QString property = it.key().split('/').last();

            if (endpoint.isNull() || !endpoint->properties().contains(property))
                continue;

            endpoint->properties().value(property)->setDisplayName(it.value());
        }
    }
}

QJsonObject Controller::deviceInfo(const Device &device)
{
    QJsonObject json = {{"key", device->key()}, {"type", device->key().mid(0, device->key().indexOf('/'))}, {"service", device->topic().mid(0, device->topic().lastIndexOf('/'))}, {"name", device->name()}, {"available", device->available()}}, properties = device->serializeProperties();

    if (!device->note().isEmpty())
        json.insert("note", device->note());

    if (!properties.isEmpty())
        json.insert("properties", properties);

    return json;
}

void Controller::httpResponse(QTcpSocket *socket, quint16 code, const QByteArray &response)
{
    QByteArray data;

    switch (code)
    {
        case 200: data = "HTTP/1.1 200 OK"; break;
        case 202: data = "HTTP/1.1 202 Accepted"; break;
        case 400: data = "HTTP/1.1 400 Bad Request"; break;
        case 401: data = "HTTP/1.1 401 Unauthorized"; break;
        case 405: data = "HTTP/1.1 405 Method Not Allowed"; break;
        default:  data = QString("HTTP/1.1 %1").arg(code).toUtf8(); break;
    }

    data.append("\r\nAccess-Control-Allow-Origin: *");
    data.append("\r\nAccess-Control-Allow-Methods: POST, OPTIONS");
    data.append("\r\nAccess-Control-Allow-Headers: Content-Type, Authorization, Mcp-Session-Id");
    data.append("\r\nAccess-Control-Expose-Headers: Mcp-Session-Id");

    if (!response.isEmpty())
        data.append("\r\nContent-Type: application/json");

    data.append(QString("\r\nContent-Length: %1").arg(response.length()).toUtf8());
    data.append(QString("\r\nMcp-Session-Id: %1").arg(m_sessionId).toUtf8());

    socket->write(data.append("\r\nConnection: close\r\n\r\n").append(response));
    socket->close();
}

void Controller::rpcResponse(QTcpSocket *socket, const QVariant &id, const QJsonValue &result)
{
    httpResponse(socket, 200, QJsonDocument({{"jsonrpc", "2.0"}, {"id", QJsonValue::fromVariant(id)}, {"result", result}}).toJson(QJsonDocument::Compact));
}

void Controller::rpcError(QTcpSocket *socket, const QVariant &id, int code, const QString &message)
{
    httpResponse(socket, 200, QJsonDocument({{"jsonrpc", "2.0"}, {"id", QJsonValue::fromVariant(id)}, {"error", QJsonObject {{"code", code}, {"message", message}}}}).toJson(QJsonDocument::Compact));
}

QJsonObject Controller::toolResult(const QString &text, bool error)
{
    return QJsonObject {{"content", QJsonArray {QJsonObject {{"type", "text"}, {"text", text}}}}, {"isError", error}};
}

void Controller::readResources(QTcpSocket *socket, const QVariant &id, const QString &uri)
{
    QList <QString> list = {"homed://services", "homed://devices"};
    QString text;

    switch (list.indexOf(uri))
    {
        case 0: // homed://services
        {
            text = QJsonDocument(QJsonArray::fromStringList(m_services)).toJson(QJsonDocument::Compact);
            break;
        }
        case 1: // homed://devices
        {
            QJsonArray array;

            for (auto it = m_devices.begin(); it != m_devices.end(); it++)
                array.append(deviceInfo(it.value()));

            text = QJsonDocument(array).toJson(QJsonDocument::Compact);
            break;
        }

        default:
        {
            rpcError(socket, id, -32602, QString("Unknown resource: %1").arg(uri));
            return;
        }
    }

    rpcResponse(socket, id, QJsonObject {{"contents", QJsonArray {QJsonObject {{"uri", uri}, {"text", text}}}}});
}






// not reviewed
void Controller::callTools(QTcpSocket *socket, const QVariant &id, const QString &name, const QJsonObject &arguments)
{
    logInfo << "tools/call" << name.toUtf8().constData() << QJsonDocument(arguments).toJson(QJsonDocument::Compact).constData();

    if (name == "list_devices")
    {
        QString service = arguments.value("service").toString(), type = arguments.value("type").toString();
        QJsonArray result;

        for (auto it = m_devices.begin(); it != m_devices.end(); it++)
        {
            if (!service.isEmpty() && !it.value()->topic().startsWith(service))
                continue;

            result.append(deviceInfo(it.value()));
        }

        rpcResponse(socket, id, toolResult(QJsonDocument(QJsonObject {{"devices", result}}).toJson(QJsonDocument::Compact)));
        return;
    }

    if (name == "get_device")
    {
        Device device = findDevice(arguments.value("device").toString());

        if (device.isNull())
        {
            rpcResponse(socket, id, toolResult(QString("Device \"%1\" not found").arg(arguments.value("device").toString()), true));
            return;
        }

        rpcResponse(socket, id, toolResult(QJsonDocument(deviceInfo(device)).toJson(QJsonDocument::Compact)));
        return;
    }

    if (name == "set_properties")
    {
        if (!m_write)
        {
            rpcResponse(socket, id, toolResult("Service is running in read-only mode", true));
            return;
        }

        QJsonArray operations = arguments.value("operations").toArray();
        QJsonArray results;
        int ok = 0, failed = 0;

        for (auto it = operations.begin(); it != operations.end(); it++)
        {
            QJsonObject op = it->toObject();
            QString deviceArg = op.value("device").toString();
            quint8 endpoint = getEndpointId(op.value("endpoint").toVariant());
            QJsonValue endpointValue = endpoint ? QJsonValue(endpoint) : QJsonValue("common");
            QJsonObject properties = op.value("properties").toObject();
            Device device = findDevice(deviceArg);

            if (device.isNull())
            {
                results.append(QJsonObject {{"device", deviceArg}, {"endpoint", endpointValue}, {"ok", false}, {"error", "device not found"}});
                failed++;
                continue;
            }

            if (properties.isEmpty())
            {
                results.append(QJsonObject {{"device", device->key()}, {"endpoint", endpointValue}, {"ok", false}, {"error", "properties is empty"}});
                failed++;
                continue;
            }

            const Endpoint &target = device->endpoints().value(static_cast <quint8> (endpoint));
            QList <QString> writableSubKeys, invalidSubKeys;

            if (!target.isNull())
                for (auto e = target->properties().begin(); e != target->properties().end(); e++)
                    if (e.value()->writable())
                        writableSubKeys.append(e.key());

            for (auto jt = properties.begin(); jt != properties.end(); jt++)
                if (!writableSubKeys.contains(jt.key()))
                    invalidSubKeys.append(jt.key());

            if (!invalidSubKeys.isEmpty() && !writableSubKeys.isEmpty())
            {
                results.append(QJsonObject {{"device", device->key()}, {"endpoint", endpointValue}, {"ok", false}, {"error", QString("unknown sub-key(s): %1; writable sub-keys for this (device, endpoint) are: %2").arg(invalidSubKeys.join(", "), writableSubKeys.join(", "))}});
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
            results.append(QJsonObject {{"device", device->key()}, {"endpoint", endpointValue}, {"topic", topic}, {"properties", QJsonArray::fromStringList(properties.keys())}, {"ok", true}});
            ok++;
        }

        logInfo << "set_properties published" << ok << "of" << operations.size() << "operations" << (failed ? QString("(%1 failed)").arg(failed).toUtf8().constData() : "");
        rpcResponse(socket, id, toolResult(QJsonDocument(QJsonObject {{"results", results}}).toJson(QJsonDocument::Compact), failed > 0 && ok == 0));
        return;
    }

    if (name == "query_history")
    {
        if (!m_services.contains("recorder"))
        {
            rpcResponse(socket, id, toolResult("homed-recorder service is not online", true));
            return;
        }

        Device device = findDevice(arguments.value("device").toString());

        if (device.isNull())
        {
            rpcResponse(socket, id, toolResult(QString("Device \"%1\" not found").arg(arguments.value("device").toString()), true));
            return;
        }

        QString property = arguments.value("property").toString();
        quint8 endpoint = getEndpointId(arguments.value("endpoint").toVariant());
        const Endpoint &target = device->endpoints().value(static_cast <quint8> (endpoint));

        if (target.isNull() || !target->properties().contains(property) || !target->properties().value(property)->history())
        {
            rpcResponse(socket, id, toolResult(QString("No history is recorded for property \"%1\" of device \"%2\"; only properties marked with \"history\" in get_device can be queried").arg(property, device->key()), true));
            return;
        }

        QString endpointKey = endpoint ? QString("%1/%2").arg(device->key()).arg(endpoint) : device->key();
        QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

        m_requests.append({socket, id, uuid, QDateTime::currentMSecsSinceEpoch() + REQUEST_TIMEOUT});
        mqttPublish(mqttTopic("command/recorder"), {{"action", "getData"}, {"id", uuid}, {"endpoint", endpointKey}, {"property", property}, {"start", arguments.value("start")}, {"end", arguments.value("end")}});

        return;
    }

    rpcError(socket, id, -32602, QString("Unknown tool: %1").arg(name));
}
//





void Controller::handleRpc(QTcpSocket *socket, const QJsonObject &request)
{
    QList <QString> list = {"initialize", "ping", "resources/list", "resources/read", "tools/list", "tools/call"};
    QString method = request.value("method").toString();
    QVariant id = request.value("id").toVariant();
    QJsonObject data = request.value("params").toObject();

    logDebug(m_debug) << "RPC called, method:" << method.toUtf8().constData();

    if (!request.contains("id"))
    {
        httpResponse(socket, 202);
        return;
    }

    switch (list.indexOf(method))
    {
        case 0: // initialize
            rpcResponse(socket, id, m_initialize);
            break;

        case 1: // ping
            rpcResponse(socket, id);
            break;

        case 2: // resources/list
            rpcResponse(socket, id, QJsonObject {{"resources", m_resources}});
            break;

        case 3: // resources/read
            readResources(socket, id, data.value("uri").toString());
            break;

        case 4: // tools/list
            rpcResponse(socket, id, QJsonObject {{"tools", m_tools}});
            break;

        case 5: // tools/call
            callTools(socket, id, data.value("name").toString(), data.value("arguments").toObject());
            break;

        default:
            rpcError(socket, id, -32601, QString("Method not found: %1").arg(method)); break;
    }
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
        QString uuid = json.value("id").toString();
        RecorderRequest request;
        bool check = false;

        for (int i = 0; i < m_requests.count(); i++)
        {
            if (m_requests.at(i).uuid != uuid)
                continue;

            request = m_requests.at(i);
            m_requests.removeAt(i);
            check = true;
            break;
        }

        if (check && m_sockets.contains(request.socket))
        {
            json.remove("id");
            rpcResponse(request.socket, request.id, toolResult(QJsonDocument(json).toJson(QJsonDocument::Compact)));
        }
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

        if (type == "recorder")
        {
            QJsonArray items = json.value("items").toArray();

            m_recordedItems.clear();

            for (auto it = items.begin(); it != items.end(); it++)
            {
                QJsonObject item = it->toObject();
                m_recordedItems.append(QString("%1/%2").arg(item.value("endpoint").toString(), item.value("property").toString()));
            }

            for (auto it = m_devices.begin(); it != m_devices.end(); it++)
                updateProperties(it.value());

            return;
        }

        if (type == "web")
        {
            QJsonObject names = json.value("names").toObject();

            m_propertyNames.clear();

            for (auto it = names.begin(); it != names.end(); it++)
                m_propertyNames.insert(it.key(), it.value().toString());

            for (auto it = m_devices.begin(); it != m_devices.end(); it++)
                updateProperties(it.value());

            return;
        }

        if (coreServices().contains(type))
            return;

        for (auto it = devices.begin(); it != devices.end(); it++)
        {
            QJsonObject item = it->toObject();
            QString name = item.value("name").toString(), note = item.value("note").toString(), id = deviceId(item, type), key, topic;
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
                device->setNote(note);
            }
            else
            {
                m_devices.insert(key, Device(new DeviceObject(key, topic, name, note)));
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

        device->parseExposes(json);
        device->setHash(hash);
        updateProperties(device);

        mqttSubscribe(mqttTopic("fd/%1").arg(device->topic()));
        mqttSubscribe(mqttTopic("fd/%1/#").arg(device->topic()));
        mqttPublish(mqttTopic("command/%1").arg(device->topic().mid(0, device->topic().lastIndexOf('/'))), {{"action", "getProperties"}, {"device", device->key().mid(device->key().indexOf('/') + 1)}, {"service", "mcp"}});
    }
    else if (subTopic.startsWith("fd/"))
    {
        QString string = subTopic.mid(subTopic.indexOf('/') + 1);
        const Device &device = findDevice(string);

        if (!device.isNull())
        {
            QList <QString> list = {"action", "event", "scene"};
            const Endpoint endpoint = device->endpoints().value(getEndpointId(string));

            if (endpoint.isNull())
                return;

            for (auto it = json.begin(); it != json.end(); it++)
            {
                if (!endpoint->properties().contains(it.key()) || list.contains(it.key().split('_').value(0)))
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

    for (auto it = m_requests.begin(); it != m_requests.end(); NULL)
    {
        if (it->socket == socket)
        {
            it = m_requests.erase(it);
            continue;
        }

        it++;
    }

    m_sockets.removeAll(socket);
    socket->deleteLater();
}

void Controller::readyRead(void)
{
    QTcpSocket *socket = reinterpret_cast <QTcpSocket*> (sender());
    QByteArray request = socket->readAll();
    QList <QString> list = QString(request).split("\r\n\r\n"), head = list.value(0).split("\r\n"), target = head.value(0).split(0x20);
    QString method = target.value(0), content = list.value(1);
    QMap <QString, QString> headers;
    QJsonObject json;

    disconnect(socket, &QTcpSocket::readyRead, this, &Controller::readyRead);
    logDebug(m_debug) << "Request" << head.value(0) << "received from" << socket->peerAddress().toString();

    for (int i = 1; i < head.count(); i++)
    {
        QList <QString> header = head.at(i).split(':');

        if (header.count() < 2)
            continue;

        headers.insert(header.value(0).toLower().trimmed(), header.value(1).trimmed());
    }

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

    if (headers.value("content-length").toInt() > content.length())
    {
        socket->waitForReadyRead();
        content.append(socket->readAll());
    }

    if (!m_token.isEmpty())
    {
        QList <QString> list = headers.value("authorization").split(0x20);

        if (list.value(0) != "Bearer" || list.value(1) != m_token)
        {
            httpResponse(socket, 401);
            return;
        }
    }

    json = QJsonDocument::fromJson(content.toUtf8()).object();

    if (!json.isEmpty())
    {
        handleRpc(socket, json);
        return;
    }

    rpcError(socket, QVariant(), -32700, "Parse error");
}

void Controller::checkRequests(void)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList <RecorderRequest> list;

    for (auto it = m_requests.begin(); it != m_requests.end(); NULL)
    {
        if (now > it->expires)
        {
            list.append(*it);
            it = m_requests.erase(it);
            continue;
        }

        it++;
    }

    for (int i = 0; i < list.count(); i++)
    {
        const RecorderRequest &request = list.at(i);

        if (!m_sockets.contains(request.socket))
            continue;

        rpcResponse(request.socket, request.id, toolResult("Request timed out", true));
    }
}
