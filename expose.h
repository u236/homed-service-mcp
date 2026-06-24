#ifndef EXPOSE_H
#define EXPOSE_H

#include <QJsonObject>
#include "device.h"

namespace Expose
{
    // Replaces device->endpoints() with what was published in `exposes` (the raw payload of
    // an expose/<service>/<topic> message). Composite items (light/cover/thermostat/...) are
    // expanded into their writable sub-keys; simple items follow their `options.type`.
    void parse(const Device &device, const QJsonObject &exposes);

    // Serializes device->endpoints() back to JSON for get_device / list_devices answers.
    QJsonObject serialize(const Device &device);
}

#endif
