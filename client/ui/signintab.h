#ifndef SIGNINTAB_H
#define SIGNINTAB_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QVideoWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGraphicsRectItem>
#include <QGeoPositionInfoSource>
#include <QGeoCoordinate>
#include <QEvent>
#include "network/apiclient.h"
#include "network/chaoxingclient.h"

class SignInTab : public QWidget
{
    Q_OBJECT
public:
    explicit SignInTab(ApiClient *api, QWidget *parent = nullptr);
    ~SignInTab() override;

private slots:
    void onCheck();
    void onSign();
    void onScanQR();
    void onCaptureFrame();
    void onCaptureResult(int id, const QImage &img);

private:
    void setupUi();
    void setBusy(bool busy);
    void doStartCamera();
    void stopCamera();
    void autoSign();
    void batchSignStart();
    void batchSignNext();
    void addLog(const QString &msg);
    void updateScanFrame();
    void showSwitchAccountDialog();
    bool eventFilter(QObject *obj, QEvent *ev) override;

    ApiClient *m_api;
    ChaoxingClient *m_cx;
    QPushButton *m_signBtn, *m_qrBtn, *m_shareBtn;
    QTextEdit *m_log;
    QVideoWidget *m_videoWidget;
    QWidget *m_cameraCard, *m_topArea, *m_btnArea, *m_logCard;
    QString m_activeId, m_signType, m_qrEnc, m_photoObjectId, m_courseName, m_shareToken;
    QString m_savedPhone, m_savedPass;

    QCamera *m_camera = nullptr;
    QMediaCaptureSession *m_captureSession = nullptr;
    QImageCapture *m_imageCapture = nullptr;
    QGraphicsView *m_graphicsView = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsVideoItem *m_videoItem = nullptr;
    QGraphicsRectItem *m_scanRect = nullptr;
    QTimer *m_scanTimer = nullptr;
    bool m_decoding = false;
    bool m_batching = false;
    int m_batchIndex = 0;
    QJsonArray m_batchAccounts;

    // Auto-check
    QTimer *m_autoTimer = nullptr;
    bool m_autoCheck = false;
    int m_lastTaskCount = 0;
    void showNotification(const QString &title, const QString &body);

    // GPS
    QGeoPositionInfoSource *m_gps = nullptr;
    double m_lat = -1, m_lon = -1;
    void requestLocation();

public:
    void pauseAutoCheck();
    void resumeAutoCheck();
    void processPendingShares();
    void onCheckAllDone() {} // stub


};

#endif
