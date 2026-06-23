#ifndef DEVICE_H
#define DEVICE_H

#include <QJsonObject>
#include <QMap>
#include <QSharedPointer>
#include <QVariantMap>

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

#endif
