#ifndef CHAOXINGCLIENT_H
#define CHAOXINGCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

/**
 * @brief Direct Chaoxing (超星/学习通) API client.
 *
 * Runs entirely on the phone — no server needed for Chaoxing calls.
 * Handles: login, course list, active sign tasks, and sign-in execution.
 */
class ChaoxingClient : public QObject
{
    Q_OBJECT
public:
    explicit ChaoxingClient(QObject *parent = nullptr);

    /** Login to Chaoxing with phone + password. */
    void login(const QString &phone, const QString &password);

    /** Get enrolled courses (requires logged-in cookies). */
    void getCourses();

    /** Get active sign tasks for a course. */
    void getActiveTasks(const QString &courseId, const QString &classId);

    /** Execute a sign-in for an active task. */
    void executeSignIn(const QString &activeId, const QString &signType,
                       const QString &name, const QString &uid,
                       const QString &fid, const QString &latitude = "-1",
                       const QString &longitude = "-1",
                       const QString &address = "中国",
                       const QString &enc = "",
                       const QString &objectId = "");

    /** Upload a photo to Chaoxing cloud storage for photo sign-in. */
    void uploadPhoto(const QByteArray &jpgData);

    /** Check all courses for active tasks. */
    void checkAllTasks();

    /** Sign in all accounts for a given task. */
    void batchSignIn(const QString &activeId, const QString &signType,
                     const QStringList &phones,
                     const QString &latitude = "-1",
                     const QString &longitude = "-1",
                     const QString &address = "中国",
                     const QString &enc = "");

    /** Current cookies (for server-side storage). */
    QMap<QString, QString> cookies() const { return m_cookies; }
    void setCookies(const QMap<QString, QString> &c) { m_cookies = c; }

    QString userName() const { return m_userName; }
    QString uid() const { return m_uid; }
    QString fid() const { return m_fid; }
    bool isLoggedIn() const { return !m_uid.isEmpty(); }

signals:
    void loginSuccess(const QString &name, const QString &uid);
    void loginFailed(const QString &error);
    void coursesReceived(const QJsonArray &courses);
    void tasksReceived(const QJsonArray &tasks);        // single course result (internal)
    void allTasksReady(const QJsonArray &tasks);        // final aggregated result (external)
    void signResult(const QJsonObject &result);
    void photoUploaded(const QString &objectId);
    void errorOccurred(const QString &error);

private:
    void onLoginReply(QNetworkReply *reply);
    void onCoursesReply(QNetworkReply *reply);
    void onTasksReply(QNetworkReply *reply, const QString &courseName);
    void onSignReply(QNetworkReply *reply);

    void processNextSignIn();
    void processNextCourse();

    static QString md5(const QString &input);
    static QString generateInfEnc();

    QNetworkAccessManager *m_mgr;
    QMap<QString, QString> m_cookies;
    QString m_userName, m_uid, m_fid;

    // Course list for processing
    struct CourseInfo { QString id, classId, name; };
    QList<CourseInfo> m_courses;
    int m_courseIndex = 0;
    QJsonArray m_allTasks;

    // Batch sign-in state
    struct BatchState {
        QString activeId, signType, lat, lon, addr, enc;
        QStringList accounts;
        int index = 0;
        QMap<QString, QString> cookies; // per-account cookies
    };
    BatchState m_batch;
    bool m_batchActive = false;
};

#endif
