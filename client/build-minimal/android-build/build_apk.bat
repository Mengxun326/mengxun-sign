@echo off
set ANDROID_HOME=C:\Users\Meng_\AppData\Local\Android\Sdk
set ANDROID_SDK_ROOT=C:\Users\Meng_\AppData\Local\Android\Sdk
set JAVA_HOME=C:\Program Files\Microsoft\jdk-17.0.7.7-hotspot
echo JAVA_HOME=%JAVA_HOME%
echo ANDROID_HOME=%ANDROID_HOME%
echo Building APK...
call gradlew.bat assembleRelease --no-daemon
