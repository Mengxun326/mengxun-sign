#include "mainwindow.h"
#include "ui/accounttab.h"
#include "ui/signintab.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include <QDir>
#ifdef ANDROID
#include <QJniObject>
#include <QtCore/private/qandroidextras_p.h>
#endif

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_api(new ApiClient(this))
{
    extern MainWindow *g_mainWindow; g_mainWindow = this;
    QSettings s("MengXun", "Sign");
    QString url = s.value("server_url").toString();
    if (!url.isEmpty()) m_api->setServerUrl(url);
    setupUi();

    connect(m_navSign, &QPushButton::clicked, this, [this]() { switchPage(0); });
    connect(m_navAccount, &QPushButton::clicked, this, [this]() {
        switchPage(1); m_api->listAccounts();
    });

    // Check for updates on startup (3s delay to let UI load first)
    QTimer::singleShot(3000, this, [this]() { checkForUpdates(); });
}

void MainWindow::setupUi()
{
    setWindowTitle("梦寻签到");
    setMinimumSize(360, 600);

    QWidget *cw = new QWidget;
    setCentralWidget(cw);
    QVBoxLayout *ml = new QVBoxLayout(cw);
    ml->setContentsMargins(0, 0, 0, 0); ml->setSpacing(0);

    // --- Header ---
    QWidget *header = new QWidget;
    header->setFixedHeight(52);
    header->setStyleSheet("background:#FFFFFF;border-bottom:1px solid #E5E5E8;");
    QHBoxLayout *hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 0, 12, 0);

    QLabel *title = new QLabel("梦寻签到");
    title->setStyleSheet("color:#1A1A1A;font-size:18px;font-weight:600;background:transparent;");

    QPushButton *settingsBtn = new QPushButton("设置");
    settingsBtn->setFixedHeight(28);
    settingsBtn->setStyleSheet(
        "QPushButton{background:transparent;color:#8E8E8E;border:none;font-size:13px;}"
        "QPushButton:hover{color:#1A1A1A;}");
    connect(settingsBtn, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString url = QInputDialog::getText(this, "服务器地址", "服务器地址:",
            QLineEdit::Normal, m_api->serverUrl(), &ok);
        if (ok && !url.isEmpty()) m_api->setServerUrl(url);
    });

    hl->addWidget(title); hl->addStretch(); hl->addWidget(settingsBtn);
    ml->addWidget(header);

    // --- Inline update progress bar (between header and pages) ---
    // QProgressDialog creates a separate top-level window which doesn't work on Android
    m_updateProgress = new QWidget;
    m_updateProgress->setObjectName("updateProgress");
    m_updateProgress->setStyleSheet(
        "#updateProgress{background:#F0F7FF;border-bottom:1px solid #B8D4F0;}");
    m_updateProgress->hide();

    QVBoxLayout *upl = new QVBoxLayout(m_updateProgress);
    upl->setContentsMargins(16, 10, 16, 10);
    upl->setSpacing(6);

    QHBoxLayout *uph = new QHBoxLayout;
    m_updateStatusLabel = new QLabel("正在下载更新...");
    m_updateStatusLabel->setStyleSheet("color:#1A1A1A;font-size:13px;font-weight:500;background:transparent;");
    m_updateCancelBtn = new QPushButton("取消");
    m_updateCancelBtn->setFixedSize(48, 24);
    m_updateCancelBtn->setStyleSheet(
        "QPushButton{background:#D8D8DC;border:none;border-radius:12px;color:#666;font-size:11px;}"
        "QPushButton:pressed{background:#C8C8CC;}");
    uph->addWidget(m_updateStatusLabel, 1);
    uph->addWidget(m_updateCancelBtn);

    m_updateProgressBar = new QProgressBar;
    m_updateProgressBar->setRange(0, 100);
    m_updateProgressBar->setValue(0);
    m_updateProgressBar->setTextVisible(false);
    m_updateProgressBar->setFixedHeight(3);
    m_updateProgressBar->setStyleSheet(
        "QProgressBar{background:#C8D8F0;border:none;border-radius:1px;}"
        "QProgressBar::chunk{background:#2C6BED;border-radius:1px;}");

    upl->addLayout(uph);
    upl->addWidget(m_updateProgressBar);

    ml->addWidget(m_updateProgress);

    // --- Pages ---
    m_stack = new QStackedWidget;
    m_signTab = new SignInTab(m_api);
    m_accountTab = new AccountTab(m_api);
    m_stack->addWidget(m_signTab);
    m_stack->addWidget(m_accountTab);
    ml->addWidget(m_stack, 1);

    // --- Bottom nav ---
    QWidget *nav = new QWidget;
    nav->setFixedHeight(56);
    nav->setStyleSheet("background:#FFFFFF;border-top:1px solid #E5E5E8;");
    QHBoxLayout *nl = new QHBoxLayout(nav);
    nl->setContentsMargins(0, 0, 0, 0); nl->setSpacing(0);

    QString btnStyle =
        "QPushButton{background:transparent;border:none;color:#8E8E8E;font-size:12px;padding:0;}"
        "QPushButton:checked{color:#1A1A1A;font-weight:600;}";

    m_navSign = new QPushButton("签到"); m_navSign->setCheckable(true); m_navSign->setChecked(true); m_navSign->setStyleSheet(btnStyle);
    m_navAccount = new QPushButton("账号"); m_navAccount->setCheckable(true); m_navAccount->setStyleSheet(btnStyle);

    nl->addWidget(m_navSign); nl->addWidget(m_navAccount);
    ml->addWidget(nav);
}

void MainWindow::checkForUpdates()
{
    qDebug() << "[Update] checking for updates...";
    QNetworkAccessManager *mgr = new QNetworkAccessManager(this);
    QNetworkReply *reply = mgr->get(QNetworkRequest(
        QUrl(m_api->serverUrl() + "/api/update/check")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, mgr]() {
        reply->deleteLater(); mgr->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[Update] version check failed:" << reply->errorString();
            return;
        }
        QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        int serverCode = json["versionCode"].toInt();
        QString ver = json["version"].toString();
        QString dlUrl = json["url"].toString();
        qDebug() << "[Update] server version:" << ver << "code:" << serverCode;

        // versionCode from AndroidManifest.xml
        if (serverCode <= 23) {
            qDebug() << "[Update] already up-to-date";
            return;
        }

        QString apkPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + "/mengxun-update.apk";
        QDir().mkpath(QFileInfo(apkPath).absolutePath());

        showUpdateDialog(ver, dlUrl, apkPath);
    });
}

void MainWindow::showUpdateDialog(const QString &ver, const QString &dlUrl, const QString &apkPath)
{
    // Overlay covers entire central widget (absolute-positioned child, not in layout)
    QWidget *overlay = new QWidget(centralWidget());
    overlay->setObjectName("updateOverlay");
    overlay->setStyleSheet("#updateOverlay{background:rgba(0,0,0,0.45);}");
    overlay->setGeometry(0, 0, centralWidget()->width(), centralWidget()->height());
    overlay->raise();
    overlay->show();

    // Use a layout inside overlay to auto-center the card
    QVBoxLayout *ol = new QVBoxLayout(overlay);
    ol->setAlignment(Qt::AlignCenter);

    // --- Card ---
    QWidget *card = new QWidget;
    card->setObjectName("updateCard");
    card->setMaximumWidth(300);
    card->setStyleSheet("#updateCard{background:#FFFFFF;border-radius:16px;}");

    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(24, 28, 24, 20);
    cl->setSpacing(0);

    // Title
    QLabel *title = new QLabel("发现新版本");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color:#1A1A1A;font-size:18px;font-weight:600;background:transparent;");
    cl->addWidget(title);
    cl->addSpacing(12);

    // Version info
    QLabel *info = new QLabel(QString("版本 %1  ·  约 26.8 MB").arg(ver));
    info->setAlignment(Qt::AlignCenter);
    info->setStyleSheet("color:#8E8E8E;font-size:13px;background:transparent;");
    cl->addWidget(info);
    cl->addSpacing(24);

    // Primary button: 立即更新
    QPushButton *btnPrimary = new QPushButton("立即更新");
    btnPrimary->setFixedHeight(44);
    btnPrimary->setStyleSheet(
        "QPushButton{background:#2C6BED;color:white;border:none;"
        "border-radius:22px;font-size:15px;font-weight:600;}"
        "QPushButton:pressed{background:#1A5AD0;}");
    cl->addWidget(btnPrimary);
    cl->addSpacing(10);

    // Secondary button: 稍后再说
    QPushButton *btnSecondary = new QPushButton("稍后再说");
    btnSecondary->setFixedHeight(40);
    btnSecondary->setStyleSheet(
        "QPushButton{background:transparent;color:#8E8E8E;border:none;font-size:13px;}"
        "QPushButton:pressed{color:#666;}");
    cl->addWidget(btnSecondary);

    ol->addWidget(card);

    // Re-center on resize
    overlay->installEventFilter(this);

    // Button actions
    connect(btnPrimary, &QPushButton::clicked, this, [this, overlay, dlUrl, apkPath]() {
        overlay->deleteLater();
        startDownload(dlUrl, apkPath);
    });
    connect(btnSecondary, &QPushButton::clicked, overlay, &QWidget::deleteLater);

    qDebug() << "[Update] custom dialog shown";
}

void MainWindow::startDownload(const QString &dlUrl, const QString &apkPath)
{
    qDebug() << "[Update] starting download:" << dlUrl;

    QFile *file = new QFile(apkPath);
    if (!file->open(QIODevice::WriteOnly)) {
        qDebug() << "[Update] cannot create file:" << apkPath;
        QMessageBox::warning(this, "下载失败", QString("无法创建文件:\n%1").arg(apkPath));
        delete file;
        return;
    }

    // Show inline progress bar (QProgressDialog doesn't work on Android!)
    m_updateProgress->show();
    m_updateProgressBar->setValue(0);
    m_updateStatusLabel->setText("正在下载更新...");

    QNetworkAccessManager *dlMgr = new QNetworkAccessManager(this);
    QNetworkRequest dlReq;
    dlReq.setUrl(QUrl(dlUrl));
    dlReq.setTransferTimeout(300000); // 5 min for 27MB
    QNetworkReply *dl = dlMgr->get(dlReq);
    m_activeDownload = dl;

    // Cancel button → abort download
    connect(m_updateCancelBtn, &QPushButton::clicked, dl, &QNetworkReply::abort);

    // Stream data to file incrementally (avoids OOM on 27MB APK)
    connect(dl, &QNetworkReply::readyRead, this, [dl, file]() {
        if (file && file->isOpen()) {
            qint64 n = file->write(dl->readAll());
            qDebug() << "[Update] chunk:" << n << "bytes, total file:" << file->size();
        }
    });

    // Update inline progress bar
    connect(dl, &QNetworkReply::downloadProgress, this,
        [this](qint64 recv, qint64 total) {
            qDebug() << "[Update] progress:" << recv << "/" << total;
            if (total > 0) {
                int pct = static_cast<int>(recv * 100 / total);
                m_updateProgressBar->setValue(pct);
                m_updateStatusLabel->setText(
                    QString("正在下载 %1 / %2 MB")
                        .arg(recv / 1048576.0, 0, 'f', 1)
                        .arg(total / 1048576.0, 0, 'f', 1));
            } else {
                m_updateStatusLabel->setText(
                    QString("已下载 %1 MB").arg(recv / 1048576.0, 0, 'f', 1));
            }
        });

    // Download finished
    connect(dl, &QNetworkReply::finished, this, [this, dl, file, dlMgr, apkPath]() {
        file->close();
        m_activeDownload = nullptr;
        m_updateProgress->hide();

        bool success = (dl->error() == QNetworkReply::NoError);
        qDebug() << "[Update] finished, success:" << success
                 << "size:" << file->size();

        // Clean up reply & manager AFTER processing (QTBUG-47230)
        dl->deleteLater();
        dlMgr->deleteLater();

        if (!success) {
            file->remove();
            delete file;
            if (dl->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::warning(this, "下载失败", dl->errorString());
            return;
        }

        if (file->size() == 0) {
            file->remove();
            delete file;
            QMessageBox::warning(this, "下载失败", "下载的文件为空");
            return;
        }
        delete file;

        installApk(apkPath);
    });
}

void MainWindow::installApk(const QString &apkPath)
{
    qDebug() << "[Update] installApk:" << apkPath;
#ifndef ANDROID
    // Desktop: just show file path
    QMessageBox::information(this, "下载完成",
        QString("APK已保存到:\n%1\n请手动安装").arg(apkPath));
    return;
#else
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid()) {
fallback:
        QMessageBox::information(this, "下载完成",
            QString("APK已保存到:\n%1\n请手动安装").arg(apkPath));
        return;
    }

    // Check REQUEST_INSTALL_PACKAGES permission (required since Android 8)
    QJniObject pm = ctx.callObjectMethod("getPackageManager",
        "()Landroid/content/pm/PackageManager;");
    bool canInstall = pm.isValid() && pm.callMethod<jboolean>("canRequestPackageInstalls");
    qDebug() << "[Update] canRequestPackageInstalls:" << canInstall;

    if (!canInstall) {
        // Directly open Settings for the user to grant "Install unknown apps"
        QJniObject action = QJniObject::fromString(
            "android.settings.MANAGE_UNKNOWN_APP_SOURCES");
        QJniObject pkgUri = QJniObject::callStaticObjectMethod(
            "android/net/Uri", "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;",
            QJniObject::fromString("package:com.mengxun.sign").object<jstring>());
        QJniObject settingsIntent("android/content/Intent",
            "(Ljava/lang/String;Landroid/net/Uri;)V",
            action.object<jstring>(), pkgUri.object());
        settingsIntent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
            0x10000000);
        ctx.callObjectMethod("startActivity", "(Landroid/content/Intent;)V",
            settingsIntent.object());
        qDebug() << "[Update] opened MANAGE_UNKNOWN_APP_SOURCES settings";

        // Non-blocking: prompt user to enable permission, then retry
        QMessageBox *permBox = new QMessageBox(this);
        permBox->setWindowTitle("需要安装权限");
        permBox->setText("请在打开的设置页面中\n启用「允许安装未知应用」\n然后点击「重试」安装更新");
        permBox->setStandardButtons(QMessageBox::Retry | QMessageBox::Cancel);
        permBox->setDefaultButton(QMessageBox::Retry);
        permBox->setAttribute(Qt::WA_DeleteOnClose);
        QString apk = apkPath;
        connect(permBox, &QDialog::finished, this, [this, apk](int result) {
            if (result == QMessageBox::Retry)
                installApk(apk);
        });
        permBox->open();
        return;
    }

    // Permission granted — build FileProvider URI and launch package installer
    QJniObject jFile("java/io/File", "(Ljava/lang/String;)V",
        QJniObject::fromString(apkPath).object<jstring>());
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider", "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        ctx.object(),
        QJniObject::fromString("com.mengxun.sign.fileprovider").object<jstring>(),
        jFile.object());
    if (!uri.isValid()) goto fallback;

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
        QJniObject::fromString("android.intent.action.VIEW").object<jstring>());
    intent.callObjectMethod("setDataAndType",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
        uri.object(),
        QJniObject::fromString("application/vnd.android.package-archive").object<jstring>());
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 1);       // GRANT_READ_URI_PERMISSION
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // ACTIVITY_NEW_TASK
    // Critical on MIUI: tell the installer this APK is from a trusted source
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Z)Landroid/content/Intent;",
        QJniObject::fromString("android.intent.extra.NOT_UNKNOWN_SOURCE").object<jstring>(),
        jboolean(true));

    // Grant URI read permission to all installer handler packages.
    // On MIUI/ColorOS, FLAG_GRANT_READ_URI_PERMISSION alone is not enough —
    // we must explicitly grantUriPermission to each package installer handler.
    grantUriPermissionToInstallers(ctx, intent, uri);

    ctx.callObjectMethod("startActivity", "(Landroid/content/Intent;)V", intent.object());
    qDebug() << "[Update] installer launched!";
#endif
}

#ifdef ANDROID
void MainWindow::grantUriPermissionToInstallers(const QJniObject &ctx,
    const QJniObject &intent, const QJniObject &uri)
{
    QJniObject pm = ctx.callObjectMethod("getPackageManager",
        "()Landroid/content/pm/PackageManager;");
    if (!pm.isValid()) return;

    QJniObject list = pm.callObjectMethod("queryIntentActivities",
        "(Landroid/content/Intent;I)Ljava/util/List;",
        intent.object(), jint(0x00010000)); // MATCH_DEFAULT_ONLY
    if (!list.isValid()) return;

    jint size = list.callMethod<jint>("size");
    qDebug() << "[Update] grantUriPermission:" << size << "handlers";

    for (jint i = 0; i < size; i++) {
        QJniObject ri = list.callObjectMethod("get", "(I)Ljava/lang/Object;", i);
        if (!ri.isValid()) continue;

        QJniObject ai = ri.getObjectField("activityInfo",
            "Landroid/content/pm/ActivityInfo;");
        if (!ai.isValid()) continue;

        QJniObject pn = ai.getObjectField("packageName", "Ljava/lang/String;");
        if (!pn.isValid()) continue;

        QString pkg = pn.toString();
        qDebug() << "[Update] granting URI perm to:" << pkg;

        ctx.callMethod<void>("grantUriPermission",
            "(Ljava/lang/String;Landroid/net/Uri;I)V",
            pn.object<jstring>(), uri.object(),
            jint(1)); // FLAG_GRANT_READ_URI_PERMISSION
    }
}
#endif

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    // Reposition update overlay when central widget resizes
    if (ev->type() == QEvent::Resize) {
        QWidget *overlay = centralWidget()->findChild<QWidget*>("updateOverlay");
        if (overlay && obj == overlay) {
            QWidget *cw = centralWidget();
            overlay->setGeometry(0, 0, cw->width(), cw->height());
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::switchPage(int index)
{
    m_stack->setCurrentIndex(index);
    m_navSign->setChecked(index == 0);
    m_navAccount->setChecked(index == 1);
    // only 2 pages now: 签到(0) and 账号(1)
}
