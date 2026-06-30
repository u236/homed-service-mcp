include(../homed-common/homed-common.pri)
include(../homed-common/homed-parser.pri)

HEADERS += \
    controller.h \
    device.h

SOURCES += \
    controller.cpp \
    device.cpp

DISTFILES += \
    deploy/data/usr/share/homed-mcp/initialize.json \
    deploy/data/usr/share/homed-mcp/resources.json \
    deploy/data/usr/share/homed-mcp/tools.json

QT += network

deploy.files = $${DISTFILES}
deploy.path = /usr/share/homed-mcp

INSTALLS += deploy
