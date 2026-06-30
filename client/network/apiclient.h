#ifndef APICLIENT_H
#define APICLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief Simplified HTTP client for XXT Sign server.
 *
 * No user accounts. Directly manages Chaoxing accounts and sign-in.
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setServerUrl(const QString &url);
    QString serverUrl() const;
    QString deviceId() const;

    // Account management
    void addAccount(const QString &phone, const QString &password, const QString &displayName);
    void addAccountDirect(const QString &phoneEncrypted, const QString &passEncrypted,
                          const QString &displayName, const QString &uid,
                          const QString &cookiesEncrypted);
    void listAccounts();
    void fetchCredentials();  // get decrypted credentials for batch sign-in
    void deleteAccount(int accountId);

    // Sign-in
    void checkTasks(const QString &phone, const QString &password);
    void executeSign(const QString &activeId, const QString &signType,
                     const QString &latitude, const QString &longitude,
                     const QString &address, const QString &enc,
                     const QString &accountIds = "");
    void executeSelected(const QString &activeId, const QString &signType,
                         const QStringList &phones,
                         const QString &latitude, const QString &longitude,
                         const QString &address, const QString &enc);

    void logSign(const QString &courseName, const QString &signType,
                 const QString &accountName, bool success, const QString &message);

    // Share sign-in
    void createShare(const QString &courseName, const QString &signType,
                     const QString &activeId, const QString &name,
                     const QString &uid, const QString &fid,
                     const QString &cookiesJson,
                     const QString &phone, const QString &password);
    void pollSharePending(const QString &token);
    void reportShareResult(int reqId, bool success);

signals:
    void accountAdded(const QJsonObject &data);
    void accountListReceived(const QJsonArray &data);
    void accountDeleted(const QString &message);
    void credentialsReceived(const QJsonArray &data);
    void tasksReceived(const QJsonObject &data);
    void signResult(const QJsonObject &data);
    void shareCreated(const QJsonObject &data);
    void sharePendingReceived(const QJsonArray &data);
    void errorOccurred(const QString &error);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    void sendPost(const QString &endpoint, const QUrlQuery &params);
    void sendGet(const QString &endpoint);
    void sendDelete(const QString &endpoint);

    QNetworkAccessManager *m_manager;
    QString m_serverUrl;
    QString m_deviceId;
    QHash<QNetworkReply *, QString> m_tags;
};

#endif
