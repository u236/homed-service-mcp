#ifndef DEVICE_H
#define DEVICE_H

#include <QJsonArray>
#include <QJsonObject>

class PropertyObject;
typedef QSharedPointer <PropertyObject> Property;

class EndpointObject;
typedef QSharedPointer <EndpointObject> Endpoint;

class DeviceObject;
typedef QSharedPointer <DeviceObject> Device;

class PropertyObject
{

public:

    PropertyObject(const QString &name) : m_name(name), m_writable(false) {}

    inline QString name(void) { return m_name; }

    inline QString displayName(void) { return m_displayName; }
    inline void setDisplayName(const QString &value) { m_displayName = value; }

    inline QString unit(void) { return m_unit; }
    inline void setUnit(const QString &value) { m_unit = value; }

    inline QString note(void) { return m_note; }
    inline void setNote(const QString &value) { m_note = value; }

    inline QVariant min(void) { return m_min; }
    inline void setMin(const QVariant &value) { m_min = value; }

    inline QVariant max(void) { return m_max; }
    inline void setMax(const QVariant &value) { m_max = value; }

    inline QVariant value(void) { return m_value; }
    inline void setValue(const QVariant &value) { m_value = value; }

    inline bool writable(void) { return m_writable; }
    inline void setWritable(bool value) { m_writable = value; }

    inline QList <QString> &enumValues(void) { return m_enumValues; }

private:

    QString m_name, m_displayName, m_unit, m_note;
    QVariant m_min, m_max, m_value;
    bool m_writable;

    QList <QString> m_enumValues;

};

class EndpointObject
{

public:

    EndpointObject(quint8 id) : m_id(id) {}

    inline quint8 id(void) { return m_id; }
    inline QMap <QString, Property> &properties(void) { return m_properties; }

    void parseExpose(const QString &exposeName, const QJsonObject &options);

private:

    quint8 m_id;
    QMap <QString, Property> m_properties;

    Property addProperty(const QString &name, bool writable = false, const QJsonObject &options = QJsonObject());

};

class DeviceObject
{

public:

    DeviceObject(const QString &key, const QString &topic, const QString &name, const QString &note) :
        m_key(key), m_topic(topic), m_name(name), m_note(note), m_available(false) {}

    inline QString key(void) { return m_key; }

    inline QString topic(void) { return m_topic; }
    inline void setTopic(const QString &value) { m_topic = value; }
    inline void clearTopic(void) { m_topic.clear(); }

    inline QString name(void) { return m_name; }
    inline void setName(const QString &value) { m_name = value; }

    inline QString note(void) { return m_note; }
    inline void setNote(const QString &value) { m_note = value; }

    inline QByteArray hash(void) { return m_hash; }
    inline void setHash(const QByteArray &value) { m_hash = value; }

    inline bool available(void) { return m_available; }
    inline void setAvailable(bool value) { m_available = value; }

    inline QMap <quint8, Endpoint> &endpoints(void) { return m_endpoints; }

    void parseExposes(const QJsonObject &exposes);
    QJsonObject serializeProperties(void);

private:

    QString m_key, m_topic, m_name, m_note;
    QByteArray m_hash;
    bool m_available;

    QMap <quint8, Endpoint> m_endpoints;

};

#endif
