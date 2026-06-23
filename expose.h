#ifndef EXPOSE_H
#define EXPOSE_H

#include <QJsonObject>
#include "device.h"

namespace Expose
{
    // Build an LLM-friendly view of a device's exposes:
    //   { endpoint_key: { properties: { sub_key: {value?, enum?, min?, max?, unit?, action?, ...}, ... } } }
    // For composite items (light/cover/thermostat/...) the bare item is expanded into its sub-keys
    // via an internal table; for unknown items the sub-key equals the item name. Current values
    // come from the device's fd/ cache.
    QJsonObject expand(const Device &device);
}

#endif
