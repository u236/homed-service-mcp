#ifndef DEVICE_H
#define DEVICE_H

#include <QMap>
#include <QSharedPointer>
#include <QString>
#include "property.h"

class EndpointObject;
typedef QSharedPointer <EndpointObject> Endpoint;

class DeviceObject;
typedef QSharedPointer <DeviceObject> Device;

class EndpointObject
{

public:

    EndpointObject(quint8 id) : m_id(id) {}

    inline quint8 id(void) { return m_id; }
    inline QMap <QString, Property> &properties(void) { return m_properties; }

private:

    quint8 m_id;
    QMap <QString, Property> m_properties;

};

class DeviceObject
{

public:

    DeviceObject(const QString &key, const QString &topic, const QString &name) :
        m_key(key), m_topic(topic), m_name(name), m_available(false) {}

    inline QString key(void) { return m_key; }
    inline QString type(void) { return m_topic.mid(0, m_topic.indexOf('/')); }
    inline QString service(void) { return m_topic.mid(0, m_topic.lastIndexOf('/')); }

    inline QString name(void) { return m_name; }
    inline void setName(const QString &value) { m_name = value; }

    inline QString topic(void) { return m_topic; }
    inline void setTopic(const QString &value) { m_topic = value; }
    inline void clearTopic(void) { m_topic.clear(); }

    inline bool available(void) { return m_available; }
    inline void setAvailable(bool value) { m_available = value; }

    inline QByteArray hash(void) { return m_hash; }
    inline void setHash(const QByteArray &value) { m_hash = value; }

    inline QMap <quint8, Endpoint> &endpoints(void) { return m_endpoints; }

private:

    QString m_key, m_topic, m_name;
    bool m_available;

    QByteArray m_hash;
    QMap <quint8, Endpoint> m_endpoints;

};

#endif
