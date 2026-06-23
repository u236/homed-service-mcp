include(../homed-common/homed-common.pri)
include(../homed-common/homed-parser.pri)

HEADERS += \
    controller.h \
    device.h \
    expose.h

SOURCES += \
    controller.cpp \
    expose.cpp

QT += network
