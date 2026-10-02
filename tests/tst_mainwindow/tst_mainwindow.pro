# MainWindow application-logic tests (host, Qt offscreen). Compiles the production sources unchanged.
QT       += core gui widgets serialport testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle
TARGET    = tst_mainwindow
TEMPLATE  = app
INCLUDEPATH += ../..
HEADERS  += ../../mainwindow.h ../../timestamplabel.h ../../rpmparser.h
SOURCES  += tst_mainwindow.cpp ../../mainwindow.cpp
FORMS    += ../../mainwindow.ui
