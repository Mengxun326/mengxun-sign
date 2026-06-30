#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QPointer>
#include "network/apiclient.h"
#ifdef ANDROID
#include <QJniObject>
#endif

class QNetworkReply;
class AccountTab;
class SignInTab;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ApiClient *api() const { return m_api; }

private:
    void setupUi();
    void switchPage(int index);
    bool eventFilter(QObject *obj, QEvent *ev) override;
    ApiClient *m_api;
    QStackedWidget *m_stack;
    AccountTab *m_accountTab;
    SignInTab *m_signTab;

    QPushButton *m_navSign, *m_navAccount;

    // Inline update progress (QProgressDialog doesn't work on Android)
    QWidget *m_updateProgress = nullptr;
    QLabel *m_updateStatusLabel = nullptr;
    QProgressBar *m_updateProgressBar = nullptr;
    QPushButton *m_updateCancelBtn = nullptr;
    QPointer<QNetworkReply> m_activeDownload;

    void checkForUpdates();
    void showUpdateDialog(const QString &ver, const QString &dlUrl, const QString &apkPath);
    void startDownload(const QString &dlUrl, const QString &apkPath);
    void installApk(const QString &apkPath);
#ifdef ANDROID
    void grantUriPermissionToInstallers(const QJniObject &ctx,
        const QJniObject &intent, const QJniObject &uri);
#endif
};

#endif
