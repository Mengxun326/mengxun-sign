QT += core gui widgets network multimedia multimediawidgets positioning core-private

TARGET = XXTSign
TEMPLATE = app
CONFIG += c++17

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    network/apiclient.cpp \
    network/chaoxingclient.cpp \
    crypto/cryptohelper.cpp \
    ui/accounttab.cpp \
    ui/signintab.cpp

HEADERS += \
    mainwindow.h \
    network/apiclient.h \
    network/chaoxingclient.h \
    crypto/cryptohelper.h \
    ui/accounttab.h \
    ui/signintab.h

INCLUDEPATH += . network crypto ui

android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_MIN_SDK_VERSION = 28
    ANDROID_TARGET_SDK_VERSION = 35

    DISTFILES += \
        android/AndroidManifest.xml \
        android/res/xml/network_security_config.xml \
        android/res/xml/file_paths.xml \
        android/res/values/styles.xml
}

win32: DEFINES += PLATFORM_WINDOWS
linux: DEFINES += PLATFORM_LINUX
android: DEFINES += PLATFORM_ANDROID
