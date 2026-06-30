#include <QApplication>
#include <QFont>
#include <QStyleFactory>
#include "mainwindow.h"
#ifdef ANDROID
#include <QJniEnvironment>
#include <QSettings>
#endif

// Global callback for ShareReceiver broadcast → wake Qt to process pending shares
MainWindow *g_mainWindow = nullptr;

#ifdef ANDROID
static void onSharePendingNative(JNIEnv *, jclass) {
    // Post to Qt main thread to avoid JNI thread issues
    QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
        if (!g_mainWindow) return;
        // Trigger share pending processing via MainWindow
        QString tok = QSettings("MengXun", "Sign").value("share_token").toString();
        if (!tok.isEmpty()) {
            g_mainWindow->api()->pollSharePending(tok);
            // The pending response will be handled by SignInTab's sharePendingReceived
        }
    }, Qt::QueuedConnection);
}
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#ifdef ANDROID
    // Register JNI native method for ShareReceiver
    QJniEnvironment env;
    if (env.isValid()) {
        jclass clazz = env->FindClass("com/mengxun/sign/ShareReceiver");
        if (clazz) {
            JNINativeMethod methods[] = {
                {"onSharePending", "()V", (void*)onSharePendingNative}
            };
            env->RegisterNatives(clazz, methods, 1);
        }
    }
#endif
    app.setApplicationName("梦寻签到");
    app.setApplicationVersion("1");
    app.setOrganizationName("MengXun");

    app.setStyle(QStyleFactory::create("Fusion"));

    // Use system default font for proper Chinese rendering
    QFont font = QApplication::font();
    font.setPointSize(10);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    // Dreamy purple theme palette
    QPalette p;
    p.setColor(QPalette::Window, QColor("#F8F7FF"));
    p.setColor(QPalette::WindowText, QColor("#2D2B3D"));
    p.setColor(QPalette::Base, QColor("#FFFFFF"));
    p.setColor(QPalette::AlternateBase, QColor("#F0EFFF"));
    p.setColor(QPalette::Text, QColor("#2D2B3D"));
    p.setColor(QPalette::Button, QColor("#6C63FF"));
    p.setColor(QPalette::ButtonText, QColor("#FFFFFF"));
    p.setColor(QPalette::Highlight, QColor("#6C63FF"));
    p.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    p.setColor(QPalette::Link, QColor("#6C63FF"));
    app.setPalette(p);

    MainWindow w;
    w.show();
    return app.exec();
}
