#include <QJsonArray>
#include "property.h"

QJsonObject PropertyObject::toJson(void)
{
    QJsonObject json;

    if (m_value.isValid())
        json.insert("value", QJsonValue::fromVariant(m_value));

    if (m_writable)
        json.insert("writable", true);

    if (!m_enumValues.isEmpty())
        json.insert("enum", QJsonArray::fromStringList(m_enumValues));

    if (m_min.isValid())
        json.insert("min", QJsonValue::fromVariant(m_min));

    if (m_max.isValid())
        json.insert("max", QJsonValue::fromVariant(m_max));

    if (!m_unit.isEmpty())
        json.insert("unit", m_unit);

    if (!m_note.isEmpty())
        json.insert("note", m_note);

    if (m_length)
        json.insert("length", m_length);

    if (!m_itemsHint.isEmpty())
        json.insert("items", m_itemsHint);

    return json;
}
