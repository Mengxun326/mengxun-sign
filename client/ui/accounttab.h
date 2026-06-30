#ifndef ACCOUNTTAB_H
#define ACCOUNTTAB_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include "network/apiclient.h"
#include "network/chaoxingclient.h"

class AccountTab : public QWidget
{
    Q_OBJECT
public:
    explicit AccountTab(ApiClient *api, QWidget *parent = nullptr);
    void refresh();

private slots:
    void onAdd();
    void onDelete();
    void onAccountsReceived(const QJsonArray &data);
    void onLocalLoginOk(const QString &name, const QString &uid);
    void onLocalLoginFail(const QString &err);

private:
    void setupUi();
    void storeToServer();

    ApiClient *m_api;
    ChaoxingClient *m_cx;
    QLineEdit *m_phone, *m_pass;
    QPushButton *m_addBtn, *m_delBtn;
    QListWidget *m_list;
    QLabel *m_countLabel;
    QList<int> m_ids;
    QString m_pendingPhone, m_pendingPass;
};

#endif
