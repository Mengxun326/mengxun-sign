#include "chaoxingclient.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QHttpMultiPart>

static const QString CX_LOGIN_URL = "https://passport2-api.chaoxing.com/v11/loginregister";
static const QString CX_COURSES_URL = "https://mooc1-2.chaoxing.com/visit/courses";
static const QString CX_TASKS_URL = "https://mobilelearn.chaoxing.com/widget/pcpick/stu/index";
static const QString CX_SIGN_URL = "https://mobilelearn.chaoxing.com/pptSign/stuSignajax";
static const QString CX_TOKEN = "4faa8662c59590c6f43ae9fe5b002b42";
static const QString CX_DES_KEY = "Z(AfY@XS";

ChaoxingClient::ChaoxingClient(QObject *parent) : QObject(parent), m_mgr(new QNetworkAccessManager(this)) {
    // Use Qt's automatic cookie management
    m_mgr->setCookieJar(new QNetworkCookieJar(m_mgr));
}

// Read ALL cookies from jar into map — cover all Chaoxing subdomains
static void syncCookiesFromJar(QMap<QString, QString> &cookies, QNetworkAccessManager *mgr) {
    if (!mgr->cookieJar()) return;
    // Query cookies from the exact URLs that were accessed during login
    const QStringList domains = {
        "https://passport2-api.chaoxing.com",
        "https://passport2.chaoxing.com",
        "https://i.chaoxing.com",
        "https://mooc1-2.chaoxing.com",
        "https://mobilelearn.chaoxing.com",
        "https://chaoxing.com",
        "https://api.chaoxing.com",
        "https://sso.chaoxing.com",
    };
    for (const auto &d : domains) {
        QUrl url(d);
        for (const auto &c : mgr->cookieJar()->cookiesForUrl(url)) {
            QString name = QString::fromUtf8(c.name());
            QString val = QString::fromUtf8(c.value());
            if (!val.isEmpty())
                cookies[name] = val;
        }
    }
    qDebug() << "[Cookies] synced" << cookies.size() << "cookies from jar:"
             << QStringList(cookies.keys()).join(",");
}

QString ChaoxingClient::md5(const QString &input) {
    return QString::fromLatin1(QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString ChaoxingClient::generateInfEnc() {
    QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    return md5(QString("token=%1&_time=%2&DESKey=%3").arg(CX_TOKEN, ts, CX_DES_KEY));
}

// ---------------------------------------------------------------------------
// Login
// ---------------------------------------------------------------------------
void ChaoxingClient::login(const QString &phone, const QString &password) {
    // Clear previous session to avoid cookie conflicts
    m_mgr->setCookieJar(new QNetworkCookieJar(m_mgr));
    m_cookies.clear();
    m_userName.clear(); m_uid.clear(); m_fid.clear();

    QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString infEnc = generateInfEnc();

    QUrl url(CX_LOGIN_URL);
    QUrlQuery q;
    q.addQueryItem("token", CX_TOKEN);
    q.addQueryItem("_time", ts);
    q.addQueryItem("inf_enc", infEnc);
    url.setQuery(q);

    QUrlQuery body;
    body.addQueryItem("uname", phone);
    body.addQueryItem("code", password);
    body.addQueryItem("loginType", "1");
    body.addQueryItem("roleSelect", "true");

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setRawHeader("User-Agent", "Mozilla/5.0");

    QNetworkReply *reply = m_mgr->post(req, body.query().toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onLoginReply(reply); });
}

void ChaoxingClient::onLoginReply(QNetworkReply *reply) {
    reply->deleteLater();
    QByteArray data = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        emit loginFailed("网络错误: " + reply->errorString());
        return;
    }

    QJsonObject json = QJsonDocument::fromJson(data).object();
    if (!json.value("status").toBool()) {
        emit loginFailed("登录失败: " + json.value("mes").toString("账号或密码错误"));
        return;
    }

    // Login OK — Qt's cookie jar auto-stores cookies from response

    // Get more cookies + user info
    QNetworkRequest req2(QUrl("https://passport2.chaoxing.com/api/cookie"));
    req2.setRawHeader("User-Agent", "Mozilla/5.0");
    QNetworkReply *r2 = m_mgr->get(req2);
    connect(r2, &QNetworkReply::finished, this, [this, r2]() {
        r2->deleteLater();

        // Get user info
        QNetworkRequest req3(QUrl("https://i.chaoxing.com/base"));
        req3.setRawHeader("User-Agent", "Mozilla/5.0");
        QNetworkReply *r3 = m_mgr->get(req3);
        connect(r3, &QNetworkReply::finished, this, [this, r3]() {
            r3->deleteLater();
            QString html = QString::fromUtf8(r3->readAll());

            // Sync cookies from jar
            syncCookiesFromJar(m_cookies, m_mgr);

            // Extract name
            QRegularExpression re(R"(class="user-name"[^>]*>([^<]+)<)");
            auto m = re.match(html);
            if (m.hasMatch()) m_userName = m.captured(1).trimmed();

            // Extract uid from HTML or cookies
            QRegularExpression reUid(R"("_uid"\s*:\s*"?(\d+))");
            auto mu = reUid.match(html);
            if (mu.hasMatch()) m_uid = mu.captured(1);
            if (m_uid.isEmpty()) m_uid = m_cookies.value("_uid");

            // Get fid from HTML or cookies
            QRegularExpression reFid(R"("fid"\s*:\s*"?(\d+))");
            auto mf = reFid.match(html);
            m_fid = mf.hasMatch() ? mf.captured(1) : m_cookies.value("fid", "0");

            if (m_uid.isEmpty()) {
                emit loginFailed("无法获取用户信息 (页面大小:" + QString::number(html.size()) + " 标题:" +
                    html.mid(html.indexOf("<title>") + 7, html.indexOf("</title>") - html.indexOf("<title>") - 7) + ")");
                return;
            }

            emit loginSuccess(m_userName, m_uid);
        });
    });
}

// ---------------------------------------------------------------------------
// Courses
// ---------------------------------------------------------------------------
void ChaoxingClient::getCourses() {
    QUrl url(CX_COURSES_URL);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    QNetworkReply *reply = m_mgr->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onCoursesReply(reply); });
}

void ChaoxingClient::onCoursesReply(QNetworkReply *reply) {
    reply->deleteLater();
    QString html = QString::fromUtf8(reply->readAll());

    // Parse courseId + classId pairs
    QRegularExpression cidRe(R"(name="courseId"[^>]*value="(\d+))");
    QRegularExpression clidRe(R"(name="classId"[^>]*value="(\d+))");
    QRegularExpression titleRe(R"(title="([^"]+))");

    auto cidIt = cidRe.globalMatch(html);
    auto clidIt = clidRe.globalMatch(html);
    auto titleIt = titleRe.globalMatch(html);

    QJsonArray courses;
    while (cidIt.hasNext() && clidIt.hasNext()) {
        auto cidM = cidIt.next();
        auto clidM = clidIt.next();
        CourseInfo ci;
        ci.id = cidM.captured(1);
        ci.classId = clidM.captured(1);

        // Find nearest non-trash title
        int cidPos = cidM.capturedStart();
        QString bestTitle;
        int bestDist = 2000;
        titleIt = titleRe.globalMatch(html); // reset iterator
        while (titleIt.hasNext()) {
            auto tM = titleIt.next();
            QString t = tM.captured(1);
            if (t == "退出" || t == "首页" || t == "登录" || t == "退课" || t == "移动到" || t.isEmpty())
                continue;
            int dist = qAbs(tM.capturedStart() - cidPos);
            if (dist < bestDist) { bestDist = dist; bestTitle = t; }
        }
        ci.name = bestTitle.isEmpty() ? QString("课程%1").arg(ci.id.right(4)) : bestTitle;

        courses.append(QJsonObject{{"course_id", ci.id}, {"class_id", ci.classId}, {"name", ci.name}});
    }

    emit coursesReceived(courses);
}

// ---------------------------------------------------------------------------
// Active tasks
// ---------------------------------------------------------------------------
void ChaoxingClient::getActiveTasks(const QString &courseId, const QString &classId) {
    QUrl url(QString("%1?courseId=%2&jclassId=%3").arg(CX_TASKS_URL, courseId, classId));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    QNetworkReply *reply = m_mgr->get(req);
    QString cName = courseId; // placeholder
    connect(reply, &QNetworkReply::finished, this, [this, reply, cName]() { onTasksReply(reply, cName); });
}

void ChaoxingClient::onTasksReply(QNetworkReply *reply, const QString &courseName) {
    reply->deleteLater();
    QString html = QString::fromUtf8(reply->readAll());

    // Find startList section
    int startIdx = html.indexOf("startList");
    int endIdx = html.indexOf("endList", startIdx);
    QString section = (startIdx >= 0 && endIdx > startIdx)
        ? html.mid(startIdx, endIdx - startIdx) : html;

    // Find all activeDetail(X, ...)
    QRegularExpression adRe(R"(activeDetail\((\d+))");
    auto it = adRe.globalMatch(section);
    QJsonArray tasks;

    while (it.hasNext()) {
        auto m = it.next();
        QString aid = m.captured(1);
        int pos = m.capturedStart();
        QString ctx = section.mid(qMax(0, pos - 50), 400);

        QString type = "normal";
        if (ctx.contains("二维码")) type = "qr";
        else if (ctx.contains("位置")) type = "location";
        else if (ctx.contains("拍照")) type = "photo";
        else if (ctx.contains("手势")) type = "gesture";

        tasks.append(QJsonObject{
            {"active_id", aid}, {"sign_type", type}, {"course_name", courseName}
        });
    }

    emit tasksReceived(tasks);
}

// ---------------------------------------------------------------------------
// Check all courses
// ---------------------------------------------------------------------------
void ChaoxingClient::checkAllTasks() {
    m_courses.clear();
    m_allTasks = QJsonArray();
    m_courseIndex = 0;

    // First get courses, then check each
    disconnect(m_mgr, nullptr, this, nullptr);
    connect(this, &ChaoxingClient::coursesReceived, this, [this](const QJsonArray &courses) {
        m_courses.clear();
        for (const auto &c : courses) {
            QJsonObject obj = c.toObject();
            CourseInfo ci;
            ci.id = obj["course_id"].toString();
            ci.classId = obj["class_id"].toString();
            ci.name = obj["name"].toString();
            if (ci.name != "退课" && ci.name != "移动到")
                m_courses.append(ci);
        }
        m_courseIndex = 0;
        m_allTasks = QJsonArray();
        if (!m_courses.isEmpty())
            processNextCourse();
        else
            emit tasksReceived(QJsonArray());
    });

    connect(this, &ChaoxingClient::tasksReceived, this, [this](const QJsonArray &tasks) {
        for (const auto &t : tasks) {
            QJsonObject obj = t.toObject();
            if (m_courseIndex <= m_courses.size())
                obj["course_name"] = m_courses[m_courseIndex - 1].name;
            m_allTasks.append(obj);
        }
        processNextCourse();
    });

    getCourses();
}

void ChaoxingClient::processNextCourse() {
    if (m_courseIndex >= m_courses.size()) {
        disconnect(this, &ChaoxingClient::coursesReceived, this, nullptr);
        disconnect(this, &ChaoxingClient::tasksReceived, this, nullptr);
        emit allTasksReady(m_allTasks);
        return;
    }
    const auto &ci = m_courses[m_courseIndex++];
    getActiveTasks(ci.id, ci.classId);
}

// ---------------------------------------------------------------------------
// Sign-in execution
// ---------------------------------------------------------------------------
void ChaoxingClient::executeSignIn(const QString &activeId, const QString &signType,
                                    const QString &name, const QString &uid,
                                    const QString &fid, const QString &latitude,
                                    const QString &longitude, const QString &address,
                                    const QString &enc, const QString &objectId) {
    Q_UNUSED(signType)
    QUrl url(CX_SIGN_URL);
    QUrlQuery q;
    q.addQueryItem("activeId", activeId);
    q.addQueryItem("uid", uid);
    q.addQueryItem("fid", fid);
    q.addQueryItem("appType", "15");
    q.addQueryItem("ifTiJiao", "1");
    q.addQueryItem("name", QUrl::toPercentEncoding(name));
    q.addQueryItem("address", address);
    q.addQueryItem("latitude", latitude);
    q.addQueryItem("longitude", longitude);
    q.addQueryItem("clientip", "");
    if (!enc.isEmpty()) q.addQueryItem("enc", enc);
    if (!objectId.isEmpty()) q.addQueryItem("objectId", objectId);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    QNetworkReply *reply = m_mgr->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onSignReply(reply); });
}

void ChaoxingClient::uploadPhoto(const QByteArray &jpgData) {
    QHttpMultiPart *mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, "image/jpeg");
    part.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"file\"; filename=\"0.jpg\"");
    part.setBody(jpgData);
    mp->append(part);

    QNetworkRequest req(QUrl("https://pan-yz.chaoxing.com/upload"));
    req.setRawHeader("User-Agent", "Mozilla/5.0");
    QNetworkReply *reply = m_mgr->post(req, mp);
    mp->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QString objectId = QString::fromUtf8(reply->readAll()).trimmed();
            emit photoUploaded(objectId);
        } else {
            emit photoUploaded(QString()); // empty = upload failed, fallback
        }
    });
}

void ChaoxingClient::onSignReply(QNetworkReply *reply) {
    reply->deleteLater();
    QString text = QString::fromUtf8(reply->readAll()).trimmed();
    bool ok = text.contains("成功") || text.contains("success");
    bool already = text.contains("已签");
    emit signResult(QJsonObject{
        {"success", ok || already},
        {"message", text},
        {"already_signed", already}
    });
}

// ---------------------------------------------------------------------------
// Batch sign-in: iterate accounts
// ---------------------------------------------------------------------------
void ChaoxingClient::batchSignIn(const QString &activeId, const QString &signType,
                                  const QStringList &phones,
                                  const QString &latitude, const QString &longitude,
                                  const QString &address, const QString &enc) {
    m_batch = BatchState{};
    m_batch.activeId = activeId;
    m_batch.signType = signType;
    m_batch.lat = latitude;
    m_batch.lon = longitude;
    m_batch.addr = address;
    m_batch.enc = enc;
    m_batch.accounts = phones;
    m_batch.index = 0;
    m_batchActive = true;

    processNextSignIn();
}

void ChaoxingClient::processNextSignIn() {
    if (!m_batchActive || m_batch.index >= m_batch.accounts.size()) {
        m_batchActive = false;
        return;
    }
    // This needs per-account login — handled externally for now
}
