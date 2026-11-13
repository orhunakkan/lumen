QT += core gui quick quickcontrols2 testlib
CONFIG += c++20 testcase
TARGET = lumen_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/battery.h ../src/cli.h ../src/hidpp.h ../src/lighting.h ../src/theme.h ../src/translator.h
SOURCES += lumen_tests.cpp ../src/battery.cpp ../src/cli.cpp ../src/hidpp.cpp ../src/lighting.cpp ../src/theme.cpp ../src/translator.cpp
