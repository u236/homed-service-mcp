#include <QJsonArray>
#include "expose.h"

namespace Expose
{
    static void mergeOptions(QJsonObject &property, const QJsonObject &options)
    {
        for (auto it = options.begin(); it != options.end(); it++)
            if (it.key() == "min" || it.key() == "max" || it.key() == "unit" || it.key() == "enum")
                property.insert(it.key(), it.value());
    }

    static void addAction(QJsonObject &properties, const QString &subKey, QJsonObject schema, const QJsonObject &subKeyOptions)
    {
        mergeOptions(schema, subKeyOptions);
        schema.insert("writable", true);
        properties.insert(subKey, schema);
    }

    static void expandItem(QJsonObject &properties, const QString &itemName, const QJsonObject &options)
    {
        QList <QString> composite = {"switch", "lock", "light", "cover", "thermostat", "button"};
        QString bareName = itemName.split('_').value(0), suffix = itemName.mid(bareName.length());
        QJsonObject itemOptions = options.value(itemName).toObject();

        switch (composite.indexOf(bareName))
        {
            case 0: // switch
                addAction(properties, "status" + suffix, QJsonObject {{"enum", QJsonArray {"on", "off", "toggle"}}}, itemOptions);
                break;

            case 1: // lock
                addAction(properties, "status" + suffix, QJsonObject {{"enum", QJsonArray {"on", "off"}}, {"note", "off = closed/locked, on = open/unlocked"}}, itemOptions);
                break;

            case 2: // light
            {
                addAction(properties, "status" + suffix, QJsonObject {{"enum", QJsonArray {"on", "off", "toggle"}}}, QJsonObject());

                QJsonArray subKeys = options.value(itemName).toArray();

                for (auto it = subKeys.begin(); it != subKeys.end(); it++)
                {
                    QString subKey = it->toString();
                    QJsonObject schema;

                    if (subKey == "level")
                        schema = QJsonObject {{"min", 0}, {"max", 254}};
                    else if (subKey == "color")
                        schema = QJsonObject {{"length", 3}, {"items", "integer 0..255"}};
                    else if (subKey == "colorTemperature")
                        schema = QJsonObject {{"min", 153}, {"max", 500}, {"unit", "mired"}};

                    addAction(properties, subKey + suffix, schema, options.value(subKey + suffix).toObject());
                }

                break;
            }

            case 3: // cover
                addAction(properties, "cover" + suffix,    QJsonObject {{"enum", QJsonArray {"open", "close", "stop"}}}, QJsonObject());
                addAction(properties, "position" + suffix, QJsonObject {{"min", 0}, {"max", 100}},                       QJsonObject());
                break;

            case 4: // thermostat
            {
                QList <QString> controls = {"targetTemperature", "systemMode", "operationMode", "fanMode", "heatMode"};

                for (auto it = controls.begin(); it != controls.end(); it++)
                {
                    QString subKey = *it + suffix;

                    if (!options.contains(subKey))
                        continue;

                    QJsonObject schema = *it == "targetTemperature" ? QJsonObject {{"unit", "°C"}} : QJsonObject();
                    addAction(properties, subKey, schema, options.value(subKey).toObject());
                }

                break;
            }

            case 5: // button
                addAction(properties, itemName, QJsonObject {{"note", "set to true to trigger"}}, QJsonObject());
                break;

            default: // simple item — sub-key == item name, writability follows options.type
            {
                QList <QString> writable = {"number", "select", "toggle", "button", "time"};
                QString type = itemOptions.value("type").toString();
                QJsonObject schema;

                if (type == "button")
                    schema.insert("note", "set to true to trigger");
                else if (type == "toggle")
                    schema.insert("enum", QJsonArray {true, false});
                else if (type == "time")
                    schema.insert("format", "HH:MM");

                if (writable.contains(type))
                    addAction(properties, itemName, schema, itemOptions);
                else
                {
                    mergeOptions(schema, itemOptions);
                    properties.insert(itemName, schema);
                }

                break;
            }
        }
    }

    QJsonObject expand(const Device &device)
    {
        QJsonObject raw = device->exposes(), result;
        QList <QString> endpoints = raw.keys();

        for (auto it = device->properties().begin(); it != device->properties().end(); it++)
        {
            QString key = it.key() ? QString::number(it.key()) : QString("common");

            if (!endpoints.contains(key))
                endpoints.append(key);
        }

        for (auto endpointIt = endpoints.begin(); endpointIt != endpoints.end(); endpointIt++)
        {
            QJsonObject endpointJson = raw.value(*endpointIt).toObject();
            QJsonArray items = endpointJson.value("items").toArray();
            QJsonObject options = endpointJson.value("options").toObject();
            QJsonObject properties;
            quint8 endpointId = *endpointIt == "common" ? 0 : static_cast <quint8> (endpointIt->toInt());
            QVariantMap currentValues = device->properties().value(endpointId);

            for (auto it = items.begin(); it != items.end(); it++)
                expandItem(properties, it->toString(), options);

            // fold in current values (may surface readonly sub-keys not listed in items, or
            // be the only source of properties if expose message hasn't arrived yet)
            for (auto v = currentValues.begin(); v != currentValues.end(); v++)
            {
                QJsonObject property = properties.value(v.key()).toObject();
                property.insert("value", QJsonValue::fromVariant(v.value()));
                properties.insert(v.key(), property);
            }

            QJsonObject endpointOut;
            endpointOut.insert("properties", properties);
            result.insert(*endpointIt, endpointOut);
        }

        return result;
    }
}
