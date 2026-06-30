#include "apiclient.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QSettings>
#include <QUuid>

static const QString DEFAULT_SERVER = "https://xxt.meng-xun.top";

ApiClient::ApiClient(QObject *parent) : QObject(parent), m_manager(new QNetworkAccessManager(this)), m_serverUrl(DEFAULT_SERVER)
{
    QSettings s("XXT", "Sign");
    QString saved = s.value("server_url").toString();
    if (!saved.isEmpty()) m_serverUrl = saved;

    // Device ID — generated once, persisted in settings
    m_deviceId = s.value("device_id").toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue("device_id", m_deviceId);
    }

    connect(m_manager, &QNetworkAccessManager::finished, this, &ApiClient::onReplyFinished);
}

QString ApiClient::deviceId() const { return m_deviceId; }

void ApiClient::setServerUrl(const QString &url) {
    m_serverUrl = url;
    QSettings("XXT", "Sign").setValue("server_url", url);
}

QString ApiClient::serverUrl() const { return m_serverUrl; }

void ApiClient::sendGet(const QString &endpoint) {
    QNetworkRequest req(QUrl(m_serverUrl + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    m_manager->get(req);
}

void ApiClient::sendDelete(const QString &endpoint) {
    QNetworkRequest req(QUrl(m_serverUrl + endpoint));
    m_manager->deleteResource(req);
}

void ApiClient::sendPost(const QString &endpoint, const QUrlQuery &params) {
    QNetworkRequest req(QUrl(m_serverUrl + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    m_manager->post(req, params.query().toUtf8());
}

void ApiClient::onReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject json = doc.isObject() ? doc.object() : QJsonObject();
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError && code == 0) {
        emit errorOccurred("网络连接失败: " + reply->errorString());
        return;
    }
    if (code >= 400) {
        emit errorOccurred(json.value("detail").toString("请求失败"));
        return;
    }

    QString path = reply->url().path();
    if (path.endsWith("/add"))         emit accountAdded(json);
    else if (path.endsWith("/add-direct")) emit accountAdded(json);
    else if (path.endsWith("/credentials")) emit credentialsReceived(arr);
    else if (path.contains("/share/create")) emit shareCreated(json);
    else if (path.contains("/share/pending")) emit sharePendingReceived(arr);
    else if (path.endsWith("/list"))   emit accountListReceived(arr.isEmpty() ? QJsonArray() : arr);
    else if (path.endsWith("/check"))  emit tasksReceived(json);
    else if (path.contains("/execute-selected")) emit signResult(json);
    else if (path.contains("/execute")) emit signResult(json);
    else if (path.endsWith("/log")) { /* just ack */ }
    // history route removed
    else if (reply->operation() == QNetworkAccessManager::DeleteOperation) emit accountDeleted(json.value("message").toString("已删除"));
}

// --- Public API ---

void ApiClient::addAccount(const QString &phone, const QString &password, const QString &displayName) {
    QUrlQuery p;
    p.addQueryItem("phone", phone);
    p.addQueryItem("password", password);
    p.addQueryItem("display_name", displayName);
    sendPost("/api/accounts/add", p);
}

void ApiClient::addAccountDirect(const QString &phoneEncrypted, const QString &passEncrypted,
                                  const QString &displayName, const QString &uid,
                                  const QString &cookiesEncrypted) {
    QUrlQuery p;
    p.addQueryItem("phone_encrypted", phoneEncrypted);
    p.addQueryItem("password_encrypted", passEncrypted);
    p.addQueryItem("display_name", displayName);
    p.addQueryItem("uid", uid);
    p.addQueryItem("cookies_encrypted", cookiesEncrypted);
    p.addQueryItem("device_id", m_deviceId);
    sendPost("/api/accounts/add", p);
}

void ApiClient::listAccounts() {
    sendGet(QString("/api/accounts/list?device_id=%1").arg(m_deviceId));
}

void ApiClient::fetchCredentials() {
    sendGet(QString("/api/accounts/credentials?device_id=%1").arg(m_deviceId));
}

void ApiClient::deleteAccount(int accountId) {
    sendDelete(QString("/api/accounts/%1?device_id=%2").arg(accountId).arg(m_deviceId));
}

void ApiClient::checkTasks(const QString &phone, const QString &password) {
    QUrlQuery p;
    p.addQueryItem("phone", phone);
    p.addQueryItem("password", password);
    sendPost("/api/sign/check", p);
}

void ApiClient::executeSign(const QString &activeId, const QString &signType,
                             const QString &latitude, const QString &longitude,
                             const QString &address, const QString &enc,
                             const QString &accountIds) {
    QUrlQuery p;
    p.addQueryItem("active_id", activeId);
    p.addQueryItem("sign_type", signType);
    p.addQueryItem("latitude", latitude);
    p.addQueryItem("longitude", longitude);
    p.addQueryItem("address", address);
    p.addQueryItem("enc", enc);
    if (!accountIds.isEmpty()) p.addQueryItem("account_ids", accountIds);
    sendPost("/api/sign/execute", p);
}

void ApiClient::executeSelected(const QString &activeId, const QString &signType,
                                 const QStringList &phones,
                                 const QString &latitude, const QString &longitude,
                                 const QString &address, const QString &enc) {
    QUrlQuery p;
    p.addQueryItem("active_id", activeId);
    p.addQueryItem("sign_type", signType);
    p.addQueryItem("latitude", latitude);
    p.addQueryItem("longitude", longitude);
    p.addQueryItem("address", address);
    p.addQueryItem("enc", enc);
    p.addQueryItem("phones", phones.join(","));
    sendPost("/api/sign/execute-selected", p);
}

void ApiClient::logSign(const QString &courseName, const QString &signType,
                         const QString &accountName, bool success, const QString &message) {
    QUrlQuery p;
    p.addQueryItem("course_name", courseName);
    p.addQueryItem("sign_type", signType);
    p.addQueryItem("account_name", accountName);
    p.addQueryItem("success", success ? "true" : "false");
    p.addQueryItem("message", message);
    sendPost("/api/sign/log", p);
}

void ApiClient::createShare(const QString &courseName, const QString &signType,
                             const QString &activeId, const QString &name,
                             const QString &uid, const QString &fid,
                             const QString &cookiesJson,
                             const QString &phone, const QString &password) {
    QUrlQuery p;
    p.addQueryItem("course_name", courseName);
    p.addQueryItem("sign_type", signType);
    p.addQueryItem("active_id", activeId);
    p.addQueryItem("name", name);
    p.addQueryItem("uid", uid);
    p.addQueryItem("fid", fid);
    p.addQueryItem("cookies_json", cookiesJson);
    p.addQueryItem("phone", phone);
    p.addQueryItem("password", password);
    sendPost("/api/share/create", p);
}

void ApiClient::pollSharePending(const QString &token) {
    sendGet(QString("/api/share/pending/%1").arg(token));
}

void ApiClient::reportShareResult(int reqId, bool success) {
    QUrlQuery p;
    p.addQueryItem("req_id", QString::number(reqId));
    p.addQueryItem("status", success ? "success" : "failed");
    sendPost("/api/share/result", p);
}
