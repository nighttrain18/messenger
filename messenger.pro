QT += core
QT += gui
QT += sql

TARGET = messenger
CONFIG += console
CONFIG += app_bundle
CONFIG += c++11

TEMPLATE = app

SOURCES += main.cpp \
    server.cpp

HEADERS += \
    server.h

