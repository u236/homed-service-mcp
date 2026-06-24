#ifndef PROPERTY_H
#define PROPERTY_H

#include <QJsonObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

class PropertyObject;
typedef QSharedPointer <PropertyObject> Property;

class PropertyObject
{

public:

    PropertyObject(const QString &name) : m_name(name), m_writable(false), m_length(0) {}

    inline QString name(void) { return m_name; }

    inline QVariant value(void) { return m_value; }
    inline void setValue(const QVariant &value) { m_value = value; }

    inline bool writable(void) { return m_writable; }
    inline void setWritable(bool value) { m_writable = value; }

    inline QStringList enumValues(void) { return m_enumValues; }
    inline void setEnumValues(const QStringList &value) { m_enumValues = value; }

    inline QVariant min(void) { return m_min; }
    inline void setMin(const QVariant &value) { m_min = value; }

    inline QVariant max(void) { return m_max; }
    inline void setMax(const QVariant &value) { m_max = value; }

    inline QString unit(void) { return m_unit; }
    inline void setUnit(const QString &value) { m_unit = value; }

    inline QString note(void) { return m_note; }
    inline void setNote(const QString &value) { m_note = value; }

    inline int length(void) { return m_length; }
    inline void setLength(int value) { m_length = value; }

    inline QString itemsHint(void) { return m_itemsHint; }
    inline void setItemsHint(const QString &value) { m_itemsHint = value; }

    inline QString displayName(void) { return m_displayName; }
    inline void setDisplayName(const QString &value) { m_displayName = value; }

    QJsonObject toJson(void);

private:

    QString m_name, m_unit, m_note, m_itemsHint, m_displayName;
    QVariant m_value, m_min, m_max;
    QStringList m_enumValues;
    bool m_writable;
    int m_length;

};

#endif
