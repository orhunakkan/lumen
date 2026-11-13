QT += core gui qml quick quickcontrols2

CONFIG += c++20 release
TARGET = lumen
TEMPLATE = app

HEADERS += \
    src/battery.h \
    src/cli.h \
    src/hidpp.h \
    src/lighting.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/battery.cpp \
    src/cli.cpp \
    src/hidpp.cpp \
    src/lighting.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
