include(../homed-common/homed-common.pri)
include(../homed-common/homed-parser.pri)

HEADERS += \
    controller.h \
    device.h

SOURCES += \
    controller.cpp \
    device.cpp

DISTFILES += $$files(deploy/data/usr/share/homed-mcp/*, true)
QT += network

deploy.files = deploy/data/usr/share/homed-mcp/*
deploy.path = /usr/share/homed-mcp

INSTALLS += deploy
