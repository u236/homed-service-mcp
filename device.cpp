#include "device.h"

Property EndpointObject::addProperty(const QString &name, bool writable, const QJsonObject &options)
{
    Property property(new PropertyObject(name));

    if (options.contains("enum"))
    {
        QVariant data = options.value("enum").toVariant();
        QList <QVariant> list;

        switch (data.type())
        {
            case QVariant::Map:
            {

                QMap <QString, QVariant> map = data.toMap();

                for (auto it = map.begin(); it != map.end(); it++)
                    list.append(it.value());

                break;
            }

            case QVariant::List: list = data.toList(); break;
            default: break;
        }

        for (auto it = list.begin(); it != list.end(); it++)
        {
            QString string = it->toString();

            if (property->enumValues().contains(string))
                continue;

            property->enumValues().append(string);
        }
    }

    if (options.contains("unit"))
        property->setUnit(options.value("unit").toString());

    if (options.contains("min"))
        property->setMin(options.value("min").toVariant());

    if (options.contains("max"))
        property->setMax(options.value("max").toVariant());

    property->setWritable(writable);
    m_properties.insert(name, property);
    return property;
}

void EndpointObject::parseExpose(const QString &exposeName, const QJsonObject &endpointOptions)
{
    QList <QString> list = {"switch", "lock", "light", "cover", "thermostat", "button"};
    QString itemName = exposeName.split('_').value(0), suffix = exposeName.mid(itemName.length());

    switch (list.indexOf(itemName))
    {
        case 0: // switch
        {
            Property property = addProperty(QString("status").append(suffix), true);
            property->enumValues() = {"on", "off", "toggle"};
            break;
        }

        case 1: // lock
        {
            Property property = addProperty(QString("status").append(suffix), true);
            property->enumValues() = {"on", "off"};
            property->setNote("on = open/unlocked, off = closed/locked");
            break;
        }

        case 2: // light
        {
            QList <QString> properties = {"level", "color", "colorTemperature", "colorMode"};
            QJsonArray items = endpointOptions.value(exposeName).toArray();
            Property status = addProperty(QString("status").append(suffix), true);

            status->enumValues() = {"on", "off", "toggle"};

            for (auto it = items.begin(); it != items.end(); it++)
            {
                QString itemName = it->toString(), propertyName = QString(itemName).append(suffix);
                QJsonObject options = endpointOptions.value(endpointOptions.contains(propertyName) ? propertyName : itemName).toObject();

                switch (properties.indexOf(itemName))
                {
                    case 0: // level
                    {
                        Property property = addProperty(propertyName, true);
                        property->setMin(options.value("min").toInt(0));
                        property->setMax(options.value("max").toInt(254));
                        break;
                    }

                    case 1: // color
                    {
                        Property property = addProperty(propertyName, true);
                        property->setNote("RGB triplet, 3 integers 0..255");
                        break;
                    }

                    case 2: // colorTemperature
                    {
                        Property property = addProperty(propertyName, true);
                        property->setMin(options.value("min").toInt(153));
                        property->setMax(options.value("max").toInt(500));
                        property->setUnit("mired");
                        break;
                    }

                    case 3: // colorMode
                    {
                        Property property = addProperty(propertyName);
                        property->setNote("true = RGB, false = CCT");
                        break;
                    }
                }
            }

            break;
        }

        case 3: // cover
        {
            Property cover = addProperty(QString("cover").append(suffix), true), position = addProperty(QString("position").append(suffix), true);
            cover->enumValues() = {"open", "close", "stop"};
            position->setMin(0);
            position->setMax(100);
            break;
        }

        case 4: // thermostat
        {
            QList <QString> properties = {"targetTemperature", "systemMode", "operationMode", "fanMode", "heatMode"};

            addProperty("temperature")->setUnit("°C");

            if (endpointOptions.contains("runningStatus"))
                addProperty("running")->setNote("true = active, false = idle");

            for (int i = 0; i < properties.length(); i++)
            {
                const QString &name = properties.at(i);

                if (endpointOptions.contains(name))
                {
                    Property property = addProperty(name, true, endpointOptions.value(name).toObject());

                    if (name != "targetTemperature")
                        continue;

                    property->setUnit("°C");
                }
            }

            break;
        }

        case 5: // button
        {
            Property property = addProperty(exposeName, true);
            property->setNote("set to true to trigger");
            break;
        }

        default:
        {
            QList <QString> writable = {"number", "select", "toggle", "button", "time"};
            QJsonObject options = endpointOptions.value(endpointOptions.contains(exposeName) ? exposeName : itemName).toObject();
            QString type = options.value("type").toString();
            Property property = addProperty(exposeName, writable.contains(type), options);

            if (!property->writable())
                break;

            switch (writable.indexOf(type))
            {
                case 2: property->enumValues() = {"true", "false"}; break; // toggle
                case 3: property->setNote("set to true to trigger"); break; // button
                case 4: property->setNote("format: HH:MM"); break; // time
                default: break;
            }

            break;
        }
    }
}

void DeviceObject::parseExposes(const QJsonObject &exposes)
{
    m_endpoints.clear();

    for (auto it = exposes.begin(); it != exposes.end(); it++)
    {
        Endpoint endpoint(new EndpointObject(it.key() == "common" ? 0 : static_cast <quint8> (it.key().toInt())));
        QJsonObject json = it.value().toObject(), options = json.value("options").toObject();
        QJsonArray items = json.value("items").toArray();

        for (auto it = items.begin(); it != items.end(); it++)
            endpoint->parseExpose(it->toString(), options);

        m_endpoints.insert(endpoint->id(), endpoint);
    }
}

QJsonObject DeviceObject::serializeProperties(void)
{
    QJsonObject json;

    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); it++)
    {
        const Endpoint &endpoint = it.value();
        QJsonObject items;

        for (auto it = endpoint->properties().begin(); it != endpoint->properties().end(); it++)
        {
            const Property &property = it.value();
            QJsonObject item;

            if (!property->displayName().isEmpty())
                item.insert("display_name", property->displayName());

            if (!property->unit().isEmpty())
                item.insert("unit", property->unit());

            if (!property->note().isEmpty())
                item.insert("note", property->note());

            if (property->min().isValid())
                item.insert("min", QJsonValue::fromVariant(property->min()));

            if (property->max().isValid())
                item.insert("max", QJsonValue::fromVariant(property->max()));

            if (property->value().isValid())
                item.insert("value", QJsonValue::fromVariant(property->value()));

            if (property->writable())
                item.insert("writable", true);

            if (property->history())
                item.insert("history", true);

            if (!property->enumValues().isEmpty())
                item.insert("enum", QJsonArray::fromStringList(property->enumValues()));

            if (item.isEmpty())
                continue;

            items.insert(it.key(), item);
        }

        if (items.isEmpty())
            continue;

        json.insert(endpoint->id() ? QString::number(endpoint->id()) : "common", items);
    }

    return json;
}
