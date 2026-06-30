QT += core gui widgets network

TARGET = XXTMinimal
TEMPLATE = app

CONFIG += c++17

SOURCES += main_minimal.cpp

android {
    ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
    ANDROID_MIN_SDK_VERSION = 28
    ANDROID_TARGET_SDK_VERSION = 35
}
