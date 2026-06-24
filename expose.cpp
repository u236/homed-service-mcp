#include <QJsonArray>
#include "expose.h"

namespace Expose
{
    static Property addProperty(const Endpoint &endpoint, const QString &subKey)
    {
        Property property = Property::create(subKey);
        endpoint->properties().insert(subKey, property);
        return property;
    }

    static void applyOptions(const Property &property, const QJsonObject &options)
    {
        if (options.contains("min"))
            property->setMin(options.value("min").toVariant());

        if (options.contains("max"))
            property->setMax(options.value("max").toVariant());

        if (options.contains("unit"))
            property->setUnit(options.value("unit").toString());

        if (options.contains("enum"))
        {
            QJsonArray array = options.value("enum").toArray();
            QStringList list;

            for (auto it = array.begin(); it != array.end(); it++)
                list.append(it->toString());

            property->setEnumValues(list);
        }
    }

    static Property addAction(const Endpoint &endpoint, const QString &subKey, const QJsonObject &subKeyOptions)
    {
        Property property = addProperty(endpoint, subKey);
        property->setWritable(true);
        applyOptions(property, subKeyOptions);
        return property;
    }

    static void expandItem(const Endpoint &endpoint, const QString &itemName, const QJsonObject &options)
    {
        QList <QString> composite = {"switch", "lock", "light", "cover", "thermostat", "button"};
        QString bareName = itemName.split('_').value(0), suffix = itemName.mid(bareName.length());
        QJsonObject itemOptions = options.value(itemName).toObject();

        switch (composite.indexOf(bareName))
        {
            case 0: // switch
            {
                Property property = addAction(endpoint, "status" + suffix, itemOptions);
                property->setEnumValues({"on", "off", "toggle"});
                break;
            }

            case 1: // lock
            {
                Property property = addAction(endpoint, "status" + suffix, itemOptions);
                property->setEnumValues({"on", "off"});
                property->setNote("off = closed/locked, on = open/unlocked");
                break;
            }

            case 2: // light
            {
                Property status = addAction(endpoint, "status" + suffix, QJsonObject());
                status->setEnumValues({"on", "off", "toggle"});

                QJsonArray subKeys = options.value(itemName).toArray();

                for (auto it = subKeys.begin(); it != subKeys.end(); it++)
                {
                    QString subKey = it->toString();
                    Property property = addAction(endpoint, subKey + suffix, options.value(subKey + suffix).toObject());

                    if (subKey == "level")
                    {
                        property->setMin(0);
                        property->setMax(254);
                    }
                    else if (subKey == "color")
                    {
                        property->setLength(3);
                        property->setItemsHint("integer 0..255");
                    }
                    else if (subKey == "colorTemperature")
                    {
                        property->setMin(153);
                        property->setMax(500);
                        property->setUnit("mired");
                    }
                }

                break;
            }

            case 3: // cover
            {
                Property cover = addAction(endpoint, "cover" + suffix, QJsonObject());
                cover->setEnumValues({"open", "close", "stop"});

                Property position = addAction(endpoint, "position" + suffix, QJsonObject());
                position->setMin(0);
                position->setMax(100);
                break;
            }

            case 4: // thermostat
            {
                QList <QString> controls = {"targetTemperature", "systemMode", "operationMode", "fanMode", "heatMode"};

                for (auto it = controls.begin(); it != controls.end(); it++)
                {
                    QString subKey = *it + suffix;

                    if (!options.contains(subKey))
                        continue;

                    Property property = addAction(endpoint, subKey, options.value(subKey).toObject());

                    if (*it == "targetTemperature")
                        property->setUnit("°C");
                }

                break;
            }

            case 5: // button
            {
                Property property = addAction(endpoint, itemName, QJsonObject());
                property->setNote("set to true to trigger");
                break;
            }

            default: // simple item — writability follows options.type
            {
                QList <QString> writableTypes = {"number", "select", "toggle", "button", "time"};
                QString type = itemOptions.value("type").toString();

                if (writableTypes.contains(type))
                {
                    Property property = addAction(endpoint, itemName, itemOptions);

                    if (type == "button")
                        property->setNote("set to true to trigger");
                    else if (type == "toggle")
                        property->setEnumValues({"true", "false"});
                    else if (type == "time")
                        property->setNote("format: HH:MM");
                }
                else
                {
                    Property property = addProperty(endpoint, itemName);
                    applyOptions(property, itemOptions);
                }

                break;
            }
        }
    }

    void parse(const Device &device, const QJsonObject &exposes)
    {
        device->endpoints().clear();

        for (auto it = exposes.begin(); it != exposes.end(); it++)
        {
            QJsonObject endpointJson = it.value().toObject();
            QJsonArray items = endpointJson.value("items").toArray();
            QJsonObject options = endpointJson.value("options").toObject();
            quint8 endpointId = it.key() == "common" ? 0 : static_cast <quint8> (it.key().toInt());

            Endpoint endpoint = Endpoint::create(endpointId);
            device->endpoints().insert(endpointId, endpoint);

            for (auto jt = items.begin(); jt != items.end(); jt++)
                expandItem(endpoint, jt->toString(), options);
        }
    }

    QJsonObject serialize(const Device &device)
    {
        QJsonObject result;

        for (auto it = device->endpoints().begin(); it != device->endpoints().end(); it++)
        {
            const Endpoint &endpoint = it.value();
            QJsonObject properties;

            for (auto jt = endpoint->properties().begin(); jt != endpoint->properties().end(); jt++)
                properties.insert(jt.key(), jt.value()->toJson());

            QJsonObject endpointOut;
            endpointOut.insert("properties", properties);
            result.insert(endpoint->id() ? QString::number(endpoint->id()) : QString("common"), endpointOut);
        }

        return result;
    }
}
