// SignInTab — minimalist redesign
// Only setupUi() changed; all functional code preserved
#include "signintab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QBuffer>
#include <QMediaDevices>
#include <QHttpMultiPart>
#include <QNetworkReply>
#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QSettings>
#include "crypto/cryptohelper.h"
#include <QGeoPositionInfoSource>
#include <QGeoCoordinate>

// Must match the key in accounttab.cpp
static const QByteArray ENC_KEY = QByteArray::fromBase64(
    "bTg0cDdrM3kxWnY2Qjl0ZkczbVg1dUJ4bU90YU54VUJBV0NMVzhIVDJtWQ==");

#ifdef ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QtCore/private/qandroidextras_p.h>
#endif

// ——— Palette ———
static const QString C_TEXT   = "#1A1A1A";
static const QString C_SUB    = "#8E8E8E";
static const QString C_BG     = "#F5F5F7";
static const QString C_SURFACE = "#FFFFFF";
static const QString C_BORDER = "#E5E5E8";
static const QString C_ACCENT = "#2C6BED";
static const QString C_DANGER = "#E53E3E";

static QString btnPrimary() {
    return "QPushButton{background:"+C_ACCENT+";color:#FFF;border:none;border-radius:6px;padding:13px;font-size:15px;font-weight:560;}"
           "QPushButton:hover{background:#2563DB;}QPushButton:disabled{background:#C5D5F5;color:#FFF;}";
}
static QString btnOutline() {
    return "QPushButton{background:transparent;color:"+C_TEXT+";border:1px solid "+C_BORDER+";border-radius:6px;padding:10px;font-size:13px;}"
           "QPushButton:hover{border-color:"+C_ACCENT+";color:"+C_ACCENT+";}";
}

SignInTab::SignInTab(ApiClient *api, QWidget *parent) : QWidget(parent), m_api(api)
{
    QSettings s("MengXun", "Sign");
    m_savedPhone = s.value("auto_phone").toString();
    m_savedPass  = s.value("auto_pass").toString();

    m_cx = new ChaoxingClient(this);
    setupUi();

    // ——— Signals (unchanged functional logic) ———
    connect(m_cx, &ChaoxingClient::loginSuccess, this, [this](const QString &, const QString &) {
        if (m_batching) {
            QString lat = QString::number(m_lat, 'f', 6);
            QString lon = QString::number(m_lon, 'f', 6);
            m_cx->executeSignIn(m_activeId, m_signType, m_cx->userName(), m_cx->uid(), m_cx->fid(),
                                lat, lon, "中国", m_qrEnc, m_photoObjectId);
        } else { m_cx->checkAllTasks(); }
    });
    connect(m_cx, &ChaoxingClient::loginFailed, this, [this](const QString &err) {
        if (m_batching) {
            addLog("  " + m_batchAccounts[m_batchIndex].toObject()["name"].toString() + ": 登录失败");
            m_batchIndex++; batchSignNext();
        } else { setBusy(false); addLog(err); }
    });
    connect(m_cx, &ChaoxingClient::allTasksReady, this, [this](const QJsonArray &tasks) {
        setBusy(false);
        if (tasks.isEmpty()) { m_lastTaskCount = 0; addLog("未检测到签到任务"); return; }
        int qr = 0, loc = 0, normal = 0, photo = 0, gesture = 0;
        for (const auto &t : tasks) {
            QString st = t.toObject()["sign_type"].toString();
            if (st == "qr") qr++; else if (st == "location") loc++;
            else if (st == "photo") photo++; else if (st == "gesture") gesture++;
            else normal++;
        }
        QStringList parts;
        if (qr > 0) parts << QString("%1个二维码").arg(qr);
        if (loc > 0) parts << QString("%1个位置").arg(loc);
        if (photo > 0) parts << QString("%1个拍照").arg(photo);
        if (gesture > 0) parts << QString("%1个手势").arg(gesture);
        if (normal > 0) parts << QString("%1个普通").arg(normal);
        QString summary = QString("检测到 %1 个签到任务 (%2)").arg(tasks.size()).arg(parts.join("，"));

        QJsonObject first = tasks.first().toObject();
        m_activeId = first["active_id"].toString();
        m_signType = first["sign_type"].toString();
        m_courseName = first["course_name"].toString();

        // Always show summary for manual checks, or if count changed for auto
        bool show = !m_autoCheck || tasks.size() != m_lastTaskCount;
        if (show) {
            addLog(m_autoCheck ? QString("[自动] %1").arg(summary) : summary);
            if (!m_autoCheck) addLog(QString("课程: %1").arg(m_courseName));
        }
        // Notification for auto-check changes
        if (m_autoCheck && tasks.size() != m_lastTaskCount) {
            showNotification("梦寻签到 · 新签到任务", summary);
        }
        m_lastTaskCount = tasks.size();

        if (m_signType == "qr" && !m_autoCheck) { m_qrBtn->setVisible(true); m_qrBtn->setText("扫码"); }
        else if (!m_autoCheck) { m_qrBtn->setVisible(false); }
        m_signBtn->setEnabled(true);
        m_shareBtn->setVisible(true);
    });
    connect(m_cx, &ChaoxingClient::signResult, this, [this](const QJsonObject &r) {
        bool ok = r["success"].toBool();
        if (m_batching) {
            QString name = m_batchIndex < m_batchAccounts.size()
                ? m_batchAccounts[m_batchIndex].toObject()["name"].toString() : "?";
            addLog(QString("  %1: %2").arg(name).arg(ok?"成功":"失败"));
            m_api->logSign(m_courseName, m_signType, name, ok, r["message"].toString());
            m_batchIndex++; batchSignNext();
        } else {
            setBusy(false);
            addLog(QString("结果: %1").arg(ok ? "成功" : "失败"));
            m_api->logSign(m_courseName, m_signType, m_cx->userName(), ok, r["message"].toString());
        }
    });
    connect(m_cx, &ChaoxingClient::errorOccurred, this, [this](const QString &e) {
        if (m_batching) return;
        setBusy(false); addLog(e);
    });
    connect(m_api, &ApiClient::errorOccurred, this, [this](const QString &e) {
        if (m_batching) return; // suppress noise during batch operations
        setBusy(false); addLog(e);
    });
    connect(m_api, &ApiClient::signResult, this, [this](const QJsonObject &d) {
        setBusy(false); addLog(QString("签到完成: %1/%2").arg(d["success_count"].toInt()).arg(d["total"].toInt()));
    });

    // Share created
    connect(m_api, &ApiClient::shareCreated, this, [this](const QJsonObject &data) {
        m_shareToken = data["token"].toString();
        QString url = data["url"].toString();
        qDebug() << "[Share] created, token:" << m_shareToken << "url:" << url;
        if (m_shareToken.isEmpty()) { addLog("分享链接生成失败"); return; }
        QSettings("MengXun", "Sign").setValue("share_token", m_shareToken);
        addLog(QString("链接已复制"));
        // Clipboard: guard against null on some Android configs
        QClipboard *cb = QApplication::clipboard();
        if (cb) cb->setText(url);
#ifdef ANDROID
        QJniObject ctx = QNativeInterface::QAndroidApplication::context();
        if (ctx.isValid()) {
            QJniObject jToken = QJniObject::fromString(m_shareToken);
            QJniObject::callStaticMethod<void>("com/mengxun/sign/ShareHelper", "start",
                "(Landroid/content/Context;Ljava/lang/String;)V", ctx.object(), jToken.object());
            qDebug() << "[Share] ShareHelper started";
        }
#endif
    });

    // Start share poll timer — runs always, checks token dynamically
    QTimer *sharePollTimer = new QTimer(this);
    sharePollTimer->setInterval(4000);
    connect(sharePollTimer, &QTimer::timeout, this, [this]() {
        QString tok = QSettings("MengXun", "Sign").value("share_token").toString();
        if (!tok.isEmpty()) m_api->pollSharePending(tok);
    });
    sharePollTimer->start();

    // Share pending — use friend's QR enc, GPS coordinates, or photo
    connect(m_api, &ApiClient::sharePendingReceived, this, [this](const QJsonArray &data) {
        if (data.isEmpty()) return;
        for (const auto &item : data) {
            QJsonObject obj = item.toObject();
            int reqId = obj["id"].toInt();
            QString frdEnc = obj["enc"].toString();
            QString frdLat = obj["lat"].toString();
            QString frdLon = obj["lon"].toString();

            // Use friend's data if provided, otherwise app's own
            QString enc = frdEnc.isEmpty() ? m_qrEnc : frdEnc;
            QString lat = frdLat.isEmpty() ? QString::number(m_lat, 'f', 6) : frdLat;
            QString lon = frdLon.isEmpty() ? QString::number(m_lon, 'f', 6) : frdLon;

            addLog(QString("[分享] 帮签: %1 enc=%2 lat=%3 lon=%4 activeId=%5")
                       .arg(obj["name"].toString())
                       .arg(enc.isEmpty() ? "无" : enc.left(20))
                       .arg(frdLat.isEmpty() ? "默认" : frdLat)
                       .arg(frdLon.isEmpty() ? "默认" : frdLon)
                       .arg(m_activeId));

            m_cx->executeSignIn(m_activeId, m_signType, m_cx->userName(), m_cx->uid(), m_cx->fid(),
                                lat, lon, "中国", enc, m_photoObjectId);
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(m_cx, &ChaoxingClient::signResult, this, [this, reqId, conn](const QJsonObject &r) {
                m_api->reportShareResult(reqId, r["success"].toBool()); disconnect(*conn);
            });
        }
    });

    m_scanTimer = new QTimer(this); m_scanTimer->setInterval(600);
    connect(m_scanTimer, &QTimer::timeout, this, &SignInTab::onCaptureFrame);
    m_autoCheck = true;
    m_autoTimer = new QTimer(this); m_autoTimer->setInterval(30000);
    connect(m_autoTimer, &QTimer::timeout, this, [this]() {
        if (m_autoCheck && !m_savedPhone.isEmpty()) m_cx->login(m_savedPhone, m_savedPass);
    });
    m_autoTimer->start();
    QTimer::singleShot(500, this, [this]() { if (!m_savedPhone.isEmpty()) onCheck(); });
}

SignInTab::~SignInTab() { stopCamera(); }
void SignInTab::addLog(const QString &msg) { m_log->append(msg); }

void SignInTab::setupUi()
{
    QVBoxLayout *ml = new QVBoxLayout(this);
    ml->setContentsMargins(20, 20, 20, 20); ml->setSpacing(14);
    setStyleSheet("background:" + C_BG + ";");

    // Account badge
    m_topArea = new QWidget;
    QHBoxLayout *tal = new QHBoxLayout(m_topArea); tal->setContentsMargins(0,0,0,0); tal->setSpacing(10);
    QLabel *badge = new QLabel(m_savedPhone.isEmpty() ? "未登录" : QString("账号 %1").arg(m_savedPhone));
    badge->setStyleSheet("color:"+C_SUB+";font-size:13px;");
    tal->addWidget(badge, 1);
    ml->addWidget(m_topArea);

    // Buttons
    m_btnArea = new QWidget;
    QVBoxLayout *bal = new QVBoxLayout(m_btnArea); bal->setContentsMargins(0,0,0,0); bal->setSpacing(10);

    m_qrBtn = new QPushButton("扫码"); m_qrBtn->setVisible(false); m_qrBtn->setStyleSheet(btnOutline());
    m_signBtn = new QPushButton("一键签到"); m_signBtn->setEnabled(false);
    m_signBtn->setStyleSheet("QPushButton{background:"+C_TEXT+";color:#FFF;border:none;border-radius:6px;padding:13px;font-size:15px;font-weight:560;}"
        "QPushButton:hover{background:#333;}QPushButton:disabled{background:#D8D8DC;color:#FFF;}");
    m_shareBtn = new QPushButton("生成分享链接"); m_shareBtn->setVisible(false); m_shareBtn->setStyleSheet(btnOutline());
    connect(m_shareBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeId.isEmpty()) return;
        QJsonObject co;
        QMap<QString,QString> ck = m_cx->cookies();
        for (auto it = ck.begin(); it != ck.end(); ++it) co[it.key()] = it.value();
        qDebug() << "[Share] creating with" << co.size() << "cookies:" << QStringList(co.keys()).join(",");
        m_api->createShare(m_courseName, m_signType, m_activeId,
            m_cx->userName(), m_cx->uid(), m_cx->fid(),
            QString::fromUtf8(QJsonDocument(co).toJson(QJsonDocument::Compact)),
            m_savedPhone, m_savedPass);
    });
    bal->addWidget(m_qrBtn); bal->addWidget(m_signBtn); bal->addWidget(m_shareBtn);

    // Login / Logout buttons
    QPushButton *loginBtn = new QPushButton(m_savedPhone.isEmpty() ? "登录" : "退出登录");
    loginBtn->setObjectName("loginBtn");
    loginBtn->setStyleSheet(m_savedPhone.isEmpty() ? btnPrimary() : btnOutline());
    connect(loginBtn, &QPushButton::clicked, this, [this, loginBtn]() {
        if (!m_savedPhone.isEmpty()) {
            // Logout
            m_savedPhone.clear();
            m_savedPass.clear();
            QSettings("MengXun", "Sign").remove("auto_phone");
            QSettings("MengXun", "Sign").remove("auto_pass");
            QLabel *badge = m_topArea->findChild<QLabel*>();
            if (badge) badge->setText("未登录");
            m_log->setPlaceholderText("请使用学习通账号登录后使用");
            loginBtn->setText("登录");
            loginBtn->setStyleSheet(btnPrimary());
            m_signBtn->setEnabled(false);
            m_qrBtn->setVisible(false);
            m_shareBtn->setVisible(false);
        } else {
            // Login
            showSwitchAccountDialog();
        }
    });
    bal->addWidget(loginBtn);
    ml->addWidget(m_btnArea);

    // Camera card — uses QGraphicsView so we can draw scan frame overlay on video
    m_cameraCard = new QWidget; m_cameraCard->setVisible(false);
    m_cameraCard->setStyleSheet("background:#000;border-radius:6px;border:1px solid "+C_BORDER+";");
    QVBoxLayout *cml = new QVBoxLayout(m_cameraCard); cml->setContentsMargins(0,0,0,0);
    m_graphicsView = new QGraphicsView;
    m_graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_graphicsView->setStyleSheet("background:#000;border:none;");
    m_scene = new QGraphicsScene(m_graphicsView);
    m_graphicsView->setScene(m_scene);
    m_videoItem = new QGraphicsVideoItem;
    m_videoItem->setAspectRatioMode(Qt::KeepAspectRatioByExpanding);
    m_scene->addItem(m_videoItem);
    cml->addWidget(m_graphicsView);
    m_videoWidget = nullptr;  // not used — video goes through QGraphicsVideoItem
    ml->addWidget(m_cameraCard);

    // Log
    m_logCard = new QWidget; m_logCard->setStyleSheet("background:"+C_SURFACE+";border-radius:6px;border:1px solid "+C_BORDER+";");
    QVBoxLayout *lcl = new QVBoxLayout(m_logCard); lcl->setContentsMargins(14,14,14,14);
    m_log = new QTextEdit; m_log->setReadOnly(true);
    m_log->setPlaceholderText(m_savedPhone.isEmpty()
        ? "请使用学习通账号登录后使用" : "正在检测中，请稍等...");
    QFont logFont = QApplication::font();
    logFont.setPointSize(13);
    m_log->setFont(logFont);
    m_log->setStyleSheet("QTextEdit{background:transparent;border:none;color:"+C_TEXT+";font-size:13px;line-height:1.8;}");
    lcl->addWidget(m_log);
    ml->addWidget(m_logCard, 1);

    connect(m_signBtn, &QPushButton::clicked, this, &SignInTab::onSign);
    connect(m_qrBtn, &QPushButton::clicked, this, &SignInTab::onScanQR);
}

// ——— All functional methods unchanged ———
void SignInTab::setBusy(bool busy) {
    m_signBtn->setEnabled(!busy && !m_activeId.isEmpty() && !m_decoding);
    m_qrBtn->setEnabled(!busy);
    if (!busy) { m_signBtn->setText("一键签到"); }
}
void SignInTab::onCheck() {
    if (m_savedPhone.isEmpty()) { addLog("请先登录"); return; }
    setBusy(true); m_log->clear();
    m_lastTaskCount = 0;
    m_activeId.clear(); m_signType.clear(); m_qrEnc.clear(); m_shareToken.clear();
    QSettings("MengXun","Sign").remove("share_token"); m_qrBtn->setVisible(false); m_shareBtn->setVisible(false);
    stopCamera(); m_cx->login(m_savedPhone, m_savedPass); }
void SignInTab::onSign() {
    if (m_savedPhone.isEmpty()) { addLog("请先登录"); return; }
    if (m_activeId.isEmpty()) { QMessageBox::warning(this,"提示","请先检测"); return; }
    if (m_signType=="qr" && m_qrEnc.isEmpty()) { onScanQR(); return; }
    if (m_signType=="location" && m_lat<0) { requestLocation(); return; }
    if (m_signType=="photo") { m_qrBtn->setVisible(true); m_qrBtn->setText("拍照签到"); onScanQR(); return; }
    autoSign(); }
void SignInTab::autoSign() { setBusy(true); m_signBtn->setText("获取列表...");
    QMetaObject::Connection *conn = new QMetaObject::Connection;
    *conn = connect(m_api, &ApiClient::accountListReceived, this, [this, conn](const QJsonArray &el) {
        disconnect(*conn); delete conn;
        if (el.isEmpty()) { QString lat=QString::number(m_lat,'f',6), lon=QString::number(m_lon,'f',6);
            m_cx->executeSignIn(m_activeId,m_signType,m_cx->userName(),m_cx->uid(),m_cx->fid(),lat,lon,"中国",m_qrEnc,m_photoObjectId); return; }
        QJsonArray creds;
        for (const auto &it:el) { QJsonObject o=it.toObject(); QString ph=CryptoHelper::aesDecrypt(o["phone_encrypted"].toString(),ENC_KEY);
            QString pw=CryptoHelper::aesDecrypt(o["password_encrypted"].toString(),ENC_KEY);
            if(ph.isEmpty()||pw.isEmpty()) continue;
            creds.append(QJsonObject{{"phone",ph},{"password",pw},{"name",o["display_name"].toString()},{"uid",o["uid"].toString()}}); }
        m_batchAccounts=creds; m_batchIndex=0; m_batching=true;
        addLog(QString("批量签到 (%1个)").arg(creds.size())); batchSignNext(); });
    m_api->listAccounts(); }
void SignInTab::batchSignNext() { if (m_batchIndex>=m_batchAccounts.size()) { m_batching=false; setBusy(false);
    addLog(QString("%1个完成").arg(m_batchAccounts.size())); return; }
    QJsonObject acc=m_batchAccounts[m_batchIndex].toObject(); QString phone=acc["phone"].toString();
    if (phone==m_savedPhone && m_cx->isLoggedIn()) { addLog(QStringLiteral("  %1: 无需登录").arg(acc["name"].toString()));
        QString lat=QString::number(m_lat,'f',6), lon=QString::number(m_lon,'f',6);
        m_cx->executeSignIn(m_activeId,m_signType,m_cx->userName(),m_cx->uid(),m_cx->fid(),lat,lon,"中国",m_qrEnc,m_photoObjectId); return; }
    m_signBtn->setText(QString("签到(%1/%2)").arg(m_batchIndex+1).arg(m_batchAccounts.size()));
    m_cx->login(phone,acc["password"].toString()); }
void SignInTab::onScanQR() { if(m_camera){stopCamera();m_scanTimer->stop();m_qrBtn->setText("扫码");return; }
#ifdef ANDROID
    auto cf=QtAndroidPrivate::checkPermission("android.permission.CAMERA");cf.waitForFinished();
    if(cf.result()!=QtAndroidPrivate::PermissionResult::Authorized){
        auto rf=QtAndroidPrivate::requestPermission("android.permission.CAMERA");rf.waitForFinished();
        if(rf.result()!=QtAndroidPrivate::PermissionResult::Authorized){addLog("相机权限被拒");return;}
    }
#endif
    doStartCamera(); }
void SignInTab::doStartCamera() { stopCamera(); const auto cams=QMediaDevices::videoInputs(); QCameraDevice cam;
    for(const auto &c:cams) if(c.position()==QCameraDevice::BackFace){cam=c;break;}
    if(cam.isNull()&&!cams.isEmpty()) cam=cams.first(); if(cam.isNull()){addLog("无摄像头");return;}
    m_camera=new QCamera(cam,this); m_captureSession=new QMediaCaptureSession(this); m_imageCapture=new QImageCapture(this);
    m_captureSession->setCamera(m_camera); m_captureSession->setVideoOutput(m_videoItem);
    m_captureSession->setImageCapture(m_imageCapture);
    connect(m_imageCapture,&QImageCapture::imageCaptured,this,&SignInTab::onCaptureResult);

    m_topArea->setVisible(false); m_btnArea->setVisible(false); m_logCard->setVisible(false);
    m_cameraCard->setVisible(true); m_camera->start(); m_decoding=false; m_scanTimer->start();

    // Fit video to view, install resize handler, draw scan frame
    m_graphicsView->viewport()->installEventFilter(this);
    QTimer::singleShot(400, this, [this]() {
        if (!m_camera || !m_graphicsView || !m_videoItem) return;
        m_graphicsView->resetTransform();
        QRectF vr = m_graphicsView->viewport()->rect();
        m_videoItem->setSize(vr.size());
        m_graphicsView->fitInView(m_videoItem, Qt::KeepAspectRatioByExpanding);
        updateScanFrame();
    });

    addLog("摄像头已开启，自动扫描中"); m_qrBtn->setText("关闭"); }
void SignInTab::stopCamera() { m_scanTimer->stop(); m_decoding=false;
    if(m_camera){m_camera->stop();delete m_camera;m_camera=nullptr;}
    if(m_captureSession){delete m_captureSession;m_captureSession=nullptr;}
    if(m_imageCapture){delete m_imageCapture;m_imageCapture=nullptr;}
    m_topArea->setVisible(true); m_btnArea->setVisible(true); m_logCard->setVisible(true);
    m_cameraCard->setVisible(false); m_qrBtn->setText("扫码"); }
void SignInTab::onCaptureFrame() { if(!m_camera||!m_imageCapture||m_decoding)return;
    if(m_camera->error()!=QCamera::NoError){stopCamera();m_qrBtn->setText("扫码");return;} m_decoding=true; m_imageCapture->capture(); }
void SignInTab::onCaptureResult(int id, const QImage &img) { Q_UNUSED(id)
    QByteArray jpg; QBuffer buf(&jpg); buf.open(QIODevice::WriteOnly); img.save(&buf,"JPEG",70);
    if(m_signType=="photo"){ m_decoding=false; stopCamera(); m_qrBtn->setVisible(false);
        connect(m_cx,&ChaoxingClient::photoUploaded,this,[this](const QString&){autoSign();},Qt::SingleShotConnection);
        m_cx->uploadPhoto(jpg); return; }
    QNetworkAccessManager *mgr=new QNetworkAccessManager(this);
    QHttpMultiPart *mp=new QHttpMultiPart(QHttpMultiPart::FormDataType); QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader,"image/jpeg");
    part.setHeader(QNetworkRequest::ContentDispositionHeader,"form-data; name=\"image\"; filename=\"qr.jpg\"");
    part.setBody(jpg); mp->append(part);
    QNetworkRequest req(QUrl(m_api->serverUrl()+"/api/sign/decode-qr")); req.setTransferTimeout(8000);
    QNetworkReply *reply=mgr->post(req,mp); mp->setParent(reply);
    static int sCnt = 0;
    connect(reply,&QNetworkReply::finished,this,[this,reply,mgr](){ reply->deleteLater(); mgr->deleteLater(); m_decoding=false; sCnt++;
        if(reply->error()!=QNetworkReply::NoError){ if(sCnt%5==0)addLog(QString("扫描中(%1)").arg(sCnt)); return; }
        QJsonObject json=QJsonDocument::fromJson(reply->readAll()).object();
        if(!json["enc"].toString().isEmpty()){ m_qrEnc=json["enc"].toString(); sCnt=0; addLog("解码成功");
#ifdef ANDROID
            QJniObject ctx=QNativeInterface::QAndroidApplication::context();
            QJniObject v=ctx.callObjectMethod("getSystemService","(Ljava/lang/String;)Ljava/lang/Object;",QJniObject::fromString("vibrator").object<jstring>());
            if(v.isValid()) v.callMethod<void>("vibrate","(J)V",jlong(50));
#endif
            stopCamera(); m_qrBtn->setVisible(false); autoSign(); }
        else if(sCnt%5==0) addLog(QString("扫描中(%1)").arg(sCnt)); }); }
void SignInTab::showNotification(const QString &t, const QString &b) {
#ifdef ANDROID
    QJniObject ctx=QNativeInterface::QAndroidApplication::context(); if(!ctx.isValid())return;
    QJniObject nm=ctx.callObjectMethod("getSystemService","(Ljava/lang/String;)Ljava/lang/Object;",QJniObject::fromString("notification").object<jstring>());
    QJniObject ch("android/app/NotificationChannel","(Ljava/lang/String;Ljava/lang/CharSequence;I)V",QJniObject::fromString("mengxun_sign").object<jstring>(),QJniObject::fromString("签到").object<jstring>(),4);
    nm.callMethod<void>("createNotificationChannel","(Landroid/app/NotificationChannel;)V",ch.object());
    QJniObject builder("android/app/Notification$Builder","(Landroid/content/Context;Ljava/lang/String;)V",ctx.object(),QJniObject::fromString("mengxun_sign").object<jstring>());
    builder=builder.callObjectMethod("setContentTitle","(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;",QJniObject::fromString(t).object());
    builder=builder.callObjectMethod("setContentText","(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;",QJniObject::fromString(b).object());
    builder=builder.callObjectMethod("setSmallIcon","(I)Landroid/app/Notification$Builder;",17301652);
    builder=builder.callObjectMethod("setAutoCancel","(Z)Landroid/app/Notification$Builder;",true);
    nm.callMethod<void>("notify","(ILandroid/app/Notification;)V",1001,builder.callObjectMethod("build","()Landroid/app/Notification;").object());
#endif
}
void SignInTab::requestLocation() {
#ifdef ANDROID
    auto cf=QtAndroidPrivate::checkPermission("android.permission.ACCESS_FINE_LOCATION"); cf.waitForFinished();
    if(cf.result()!=QtAndroidPrivate::PermissionResult::Authorized){auto rf=QtAndroidPrivate::requestPermission("android.permission.ACCESS_FINE_LOCATION");rf.waitForFinished();
        if(rf.result()!=QtAndroidPrivate::PermissionResult::Authorized){addLog("位置权限拒绝");return;}}
#endif
    if(!m_gps){ m_gps=QGeoPositionInfoSource::createDefaultSource(this); if(!m_gps){addLog("无GPS");return;}
        connect(m_gps,&QGeoPositionInfoSource::positionUpdated,this,[this](const QGeoPositionInfo &i){m_lat=i.coordinate().latitude();m_lon=i.coordinate().longitude();addLog(QString("GPS:%.4f,%.4f").arg(m_lat).arg(m_lon));m_gps->stopUpdates();autoSign();});
        connect(m_gps,&QGeoPositionInfoSource::errorOccurred,this,[this](QGeoPositionInfoSource::Error){addLog("GPS失败");autoSign();}); }
    m_gps->startUpdates(); addLog("获取GPS..."); QTimer::singleShot(10000,this,[this](){if(m_lat<0&&m_gps){m_gps->stopUpdates();addLog("GPS超时");autoSign();}}); }
void SignInTab::pauseAutoCheck() { if(m_autoCheck&&m_autoTimer->isActive()) m_autoTimer->stop(); }
void SignInTab::resumeAutoCheck() { if(m_autoCheck&&!m_autoTimer->isActive()) m_autoTimer->start(); }
void SignInTab::processPendingShares() { QString t=QSettings("MengXun","Sign").value("share_token").toString(); if(!t.isEmpty()){m_shareToken=t;m_api->pollSharePending(t);addLog("[分享]检查...");} }
bool SignInTab::eventFilter(QObject *obj, QEvent *ev) {
    if (m_graphicsView && obj == m_graphicsView->viewport()
        && ev->type() == QEvent::Resize && m_videoItem) {
        m_graphicsView->resetTransform();
        m_videoItem->setSize(m_graphicsView->viewport()->size());
        m_graphicsView->fitInView(m_videoItem, Qt::KeepAspectRatioByExpanding);
        updateScanFrame();
    }
    return QWidget::eventFilter(obj, ev);
}
void SignInTab::updateScanFrame() { if(!m_scene||!m_videoItem||!m_graphicsView)return;
    m_scanRect=nullptr;
    // Clear all items except video, then redraw
    QList<QGraphicsItem*> items=m_scene->items();
    for(auto *it:items) if(it!=m_videoItem){ m_scene->removeItem(it); delete it; }
    QRectF vr = m_graphicsView->mapToScene(m_graphicsView->viewport()->rect()).boundingRect();
    qreal vw=vr.width(), vh=vr.height(), ox=vr.x(), oy=vr.y();
    qreal bs=qMin(vw,vh)*0.6, x=ox+(vw-bs)/2, y=oy+(vh-bs)/2;
    qreal o=0.35; QColor oc(0,0,0,(int)(o*255));
    m_scene->addRect(ox,oy,vw,y-oy,QPen(Qt::NoPen),QBrush(oc));
    m_scene->addRect(ox,y+bs,vw,oy+vh-y-bs,QPen(Qt::NoPen),QBrush(oc));
    m_scene->addRect(ox,y,x-ox,bs,QPen(Qt::NoPen),QBrush(oc));
    m_scene->addRect(x+bs,y,ox+vw-x-bs,bs,QPen(Qt::NoPen),QBrush(oc));
    m_scanRect=m_scene->addRect(x,y,bs,bs,QPen(QColor(0x2C,0x6B,0xED),1));
    qreal bl=bs/5; QPen cp(QColor(0x2C,0x6B,0xED),2);
    m_scene->addLine(x,y+bl,x,y,cp);m_scene->addLine(x,y,x+bl,y,cp);
    m_scene->addLine(x+bs-bl,y,x+bs,y,cp);m_scene->addLine(x+bs,y,x+bs,y+bl,cp);
    m_scene->addLine(x,y+bs-bl,x,y+bs,cp);m_scene->addLine(x,y+bs,x+bl,y+bs,cp);
    m_scene->addLine(x+bs-bl,y+bs,x+bs,y+bs,cp);m_scene->addLine(x+bs,y+bs-bl,x+bs,y+bs,cp);
}

void SignInTab::showSwitchAccountDialog()
{
    // Overlay covers entire tab
    QWidget *overlay = new QWidget(this);
    overlay->setObjectName("switchOverlay");
    overlay->setStyleSheet("#switchOverlay{background:rgba(0,0,0,0.45);}");
    overlay->setGeometry(0, 0, width(), height());
    overlay->raise();
    overlay->show();

    // --- Card ---
    QWidget *card = new QWidget;
    card->setFixedWidth(qMin(width() - 48, 300));
    card->setStyleSheet("#switchCard{background:#FFFFFF;border-radius:16px;}");

    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(24, 28, 24, 20);
    cl->setSpacing(0);

    bool isLogin = m_savedPhone.isEmpty();

    // Title
    QLabel *title = new QLabel(isLogin ? "登录" : "切换账号");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("color:#1A1A1A;font-size:18px;font-weight:600;background:transparent;");
    cl->addWidget(title);
    cl->addSpacing(20);

    // Phone field
    QLineEdit *phoneEdit = new QLineEdit(m_savedPhone);
    phoneEdit->setPlaceholderText("手机号");
    phoneEdit->setStyleSheet(
        "QLineEdit{background:#F5F5F7;border:1px solid #E5E5E8;border-radius:8px;"
        "padding:12px;font-size:14px;color:#1A1A1A;}"
        "QLineEdit:focus{border-color:#2C6BED;}");
    cl->addWidget(phoneEdit);
    cl->addSpacing(12);

    // Password field
    QLineEdit *passEdit = new QLineEdit(m_savedPass);
    passEdit->setPlaceholderText("密码");
    passEdit->setEchoMode(QLineEdit::Password);
    passEdit->setStyleSheet(phoneEdit->styleSheet());
    cl->addWidget(passEdit);
    cl->addSpacing(24);

    // Confirm button
    QPushButton *btnConfirm = new QPushButton(isLogin ? "登录" : "确认切换");
    btnConfirm->setFixedHeight(44);
    btnConfirm->setStyleSheet(
        "QPushButton{background:#2C6BED;color:white;border:none;"
        "border-radius:22px;font-size:15px;font-weight:600;}"
        "QPushButton:pressed{background:#1A5AD0;}");
    cl->addWidget(btnConfirm);
    cl->addSpacing(10);

    // Cancel button
    QPushButton *btnCancel = new QPushButton("取消");
    btnCancel->setFixedHeight(40);
    btnCancel->setStyleSheet(
        "QPushButton{background:transparent;color:#8E8E8E;border:none;font-size:13px;}"
        "QPushButton:pressed{color:#666;}");
    cl->addWidget(btnCancel);

    // Center card in overlay using stretches
    QVBoxLayout *ol = new QVBoxLayout(overlay);
    ol->addStretch(1);
    ol->addWidget(card, 0, Qt::AlignHCenter);
    ol->addStretch(1);

    // Actions
    connect(btnConfirm, &QPushButton::clicked, this, [this, overlay, phoneEdit, passEdit]() {
        QString ph = phoneEdit->text().trimmed();
        QString pw = passEdit->text().trimmed();
        if (ph.isEmpty() || pw.isEmpty()) return;
        m_savedPhone = ph;
        m_savedPass = pw;
        QSettings("MengXun", "Sign").setValue("auto_phone", ph);
        QSettings("MengXun", "Sign").setValue("auto_pass", pw);
        // Update badge
        QLabel *badge = m_topArea->findChild<QLabel*>();
        if (badge) badge->setText(QString("账号 %1").arg(ph));
        m_log->setPlaceholderText("正在检测中，请稍等...");
        // Update login button → now shows "退出登录"
        QPushButton *btn = findChild<QPushButton*>("loginBtn");
        if (btn) { btn->setText("退出登录"); btn->setStyleSheet(btnOutline()); }
        overlay->deleteLater();
    });
    connect(btnCancel, &QPushButton::clicked, overlay, &QWidget::deleteLater);
}
