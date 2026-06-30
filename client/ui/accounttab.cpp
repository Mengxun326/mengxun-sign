#include "accounttab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include "crypto/cryptohelper.h"

static const QByteArray ENC_KEY = QByteArray::fromBase64(
    "bTg0cDdrM3kxWnY2Qjl0ZkczbVg1dUJ4bU90YU54VUJBV0NMVzhIVDJtWQ==");
static const QString C_TEXT="#1A1A1A", C_SUB="#8E8E8E", C_BG="#F5F5F7", C_SURF="#FFFFFF", C_BORD="#E5E5E8", C_ACC="#2C6BED";

AccountTab::AccountTab(ApiClient *api, QWidget *parent) : QWidget(parent), m_api(api) {
    m_cx = new ChaoxingClient(this);
    setupUi();
    connect(m_api, &ApiClient::accountAdded, this, [this](const QJsonObject&){refresh();});
    connect(m_api, &ApiClient::accountListReceived, this, &AccountTab::onAccountsReceived);
    connect(m_api, &ApiClient::accountDeleted, this, [this](const QString&){refresh();});
    connect(m_api, &ApiClient::errorOccurred, this, [this](const QString&){m_addBtn->setEnabled(true);m_addBtn->setText("添加");});
    connect(m_cx, &ChaoxingClient::loginSuccess, this, &AccountTab::onLocalLoginOk);
    connect(m_cx, &ChaoxingClient::loginFailed, this, &AccountTab::onLocalLoginFail);
}

void AccountTab::setupUi() {
    QVBoxLayout *ml = new QVBoxLayout(this);
    ml->setContentsMargins(20,20,20,20); ml->setSpacing(14);
    setStyleSheet("background:"+C_BG+";");

    QLabel *title = new QLabel("账号");
    title->setStyleSheet("font-size:22px;font-weight:600;color:"+C_TEXT+";");

    m_countLabel = new QLabel(""); m_countLabel->setStyleSheet("color:"+C_SUB+";font-size:13px;");
    ml->addWidget(title); ml->addWidget(m_countLabel);

    // Add form
    QWidget *ac = new QWidget; ac->setStyleSheet("background:"+C_SURF+";border-radius:6px;border:1px solid "+C_BORD+";");
    QVBoxLayout *acl = new QVBoxLayout(ac); acl->setContentsMargins(16,14,16,14); acl->setSpacing(10);

    m_phone = new QLineEdit; m_phone->setPlaceholderText("手机号");
    m_phone->setStyleSheet("QLineEdit{background:"+C_BG+";border:1px solid "+C_BORD+";border-radius:4px;padding:12px;font-size:14px;color:"+C_TEXT+";}QLineEdit:focus{border-color:"+C_ACC+";}");
    m_pass = new QLineEdit; m_pass->setPlaceholderText("学习通密码"); m_pass->setEchoMode(QLineEdit::Password); m_pass->setStyleSheet(m_phone->styleSheet());

    m_addBtn = new QPushButton("添加账号");
    m_addBtn->setStyleSheet("QPushButton{background:"+C_ACC+";color:#FFF;border:none;border-radius:4px;padding:12px;font-size:14px;font-weight:560;}QPushButton:hover{background:#2563DB;}QPushButton:disabled{background:#C5D5F5;}");
    acl->addWidget(m_phone); acl->addWidget(m_pass); acl->addWidget(m_addBtn);
    ml->addWidget(ac);

    // List
    QWidget *lc = new QWidget; lc->setStyleSheet("background:"+C_SURF+";border-radius:6px;border:1px solid "+C_BORD+";");
    QVBoxLayout *lcl = new QVBoxLayout(lc); lcl->setContentsMargins(16,14,16,14); lcl->setSpacing(6);
    m_list = new QListWidget; m_list->setMinimumHeight(120);
    m_list->setStyleSheet("QListWidget{background:transparent;border:none;}"
        "QListWidget::item{background:"+C_BG+";border-radius:4px;padding:12px;margin:2px 0;font-size:13px;color:"+C_TEXT+";}"
        "QListWidget::item:selected{background:#F0F4FF;}"
        "QListWidget::item:hover{background:#F0F4FF;}");
    m_delBtn = new QPushButton("删除选中");
    m_delBtn->setStyleSheet("QPushButton{background:transparent;color:#E53E3E;border:1px solid #E53E3E;border-radius:4px;padding:8px;font-size:13px;}");
    lcl->addWidget(m_list); lcl->addWidget(m_delBtn);
    ml->addWidget(lc, 1);

    connect(m_addBtn,&QPushButton::clicked,this,&AccountTab::onAdd);
    connect(m_delBtn,&QPushButton::clicked,this,&AccountTab::onDelete);
    connect(m_pass,&QLineEdit::returnPressed,this,&AccountTab::onAdd);
}
void AccountTab::onAdd() { QString p=m_phone->text().trimmed(),w=m_pass->text().trimmed(); if(p.isEmpty()||w.isEmpty()){QMessageBox::warning(this,"提示","请输入");return;}
    m_addBtn->setEnabled(false); m_addBtn->setText("验证..."); m_pendingPhone=p; m_pendingPass=w; m_cx->login(p,w); }
void AccountTab::onLocalLoginOk(const QString &name, const QString &uid) { m_addBtn->setText("存储...");
    QString pe=CryptoHelper::aesEncrypt(m_pendingPhone,ENC_KEY), pw=CryptoHelper::aesEncrypt(m_pendingPass,ENC_KEY);
    QJsonObject co; for(auto it=m_cx->cookies().begin();it!=m_cx->cookies().end();++it) co[it.key()]=it.value();
    m_api->addAccountDirect(pe,pw,name.isEmpty()?m_pendingPhone.right(4):name,uid,
        QString::fromUtf8(QJsonDocument(co).toJson(QJsonDocument::Compact))); }
void AccountTab::onLocalLoginFail(const QString &err) { m_addBtn->setEnabled(true); m_addBtn->setText("添加"); QMessageBox::warning(this,"失败",err); }
void AccountTab::onDelete() { int r=m_list->currentRow(); if(r<0||r>=m_ids.size()){QMessageBox::warning(this,"提示","请选择");return;}
    if(QMessageBox::question(this,"确认","删除？")==QMessageBox::Yes) m_api->deleteAccount(m_ids[r]); }
void AccountTab::refresh() { m_api->listAccounts(); }
void AccountTab::onAccountsReceived(const QJsonArray &data) { m_list->clear(); m_ids.clear(); m_countLabel->setText(QString("共 %1 个账号").arg(data.size()));
    if(data.isEmpty()){m_list->addItem("暂无账号");return;}
    for(const auto &i:data){QJsonObject a=i.toObject(); m_list->addItem(a["display_name"].toString()); m_ids.append(a["id"].toInt());
        if(!m_phone->text().isEmpty()){m_phone->clear();m_pass->clear();m_addBtn->setEnabled(true);m_addBtn->setText("添加账号");}} }
