#include "historytab.h"
#include <QVBoxLayout>
#include <QLabel>

static const QString C_TEXT="#1A1A1A", C_SUB="#8E8E8E", C_BG="#F5F5F7", C_SURF="#FFFFFF", C_BORD="#E5E5E8", C_ACC="#2C6BED";

HistoryTab::HistoryTab(ApiClient *api, QWidget *parent) : QWidget(parent), m_api(api) {
    setupUi();
    connect(m_api, &ApiClient::historyReceived, this, [this](const QJsonArray &data) {
        m_list->clear(); m_countLabel->setText(QString("共 %1 条").arg(data.size()));
        if (data.isEmpty()) { m_list->addItem("暂无签到记录"); return; }
        for (const auto &item : data) {
            QJsonObject o = item.toObject();
            QString icon = o["success"].toBool() ? "✓" : "✗";
            QString type = o["sign_type"].toString();
            QString tn = (type=="qr")?"二维码":(type=="location")?"位置":(type=="photo")?"拍照":(type=="gesture")?"手势":"普通";
            QString time = o["time"].toString().replace("T"," ").left(16);
            m_list->addItem(QString("%1  [%2]  %3  %4  %5")
                .arg(icon, tn, o["account_name"].toString(), time, o["course_name"].toString()));
        }
    });
}
void HistoryTab::setupUi() {
    QVBoxLayout *ml = new QVBoxLayout(this);
    ml->setContentsMargins(20,20,20,20); ml->setSpacing(10);
    setStyleSheet("background:"+C_BG+";");

    QLabel *title = new QLabel("历史"); title->setStyleSheet("font-size:22px;font-weight:600;color:"+C_TEXT+";");
    m_countLabel = new QLabel(""); m_countLabel->setStyleSheet("color:"+C_SUB+";font-size:13px;");

    QWidget *lc = new QWidget; lc->setStyleSheet("background:"+C_SURF+";border-radius:6px;border:1px solid "+C_BORD+";");
    QVBoxLayout *lcl = new QVBoxLayout(lc); lcl->setContentsMargins(16,14,16,14);
    m_list = new QListWidget; m_list->setStyleSheet(
        "QListWidget{background:transparent;border:none;}"
        "QListWidget::item{background:"+C_BG+";border-radius:4px;padding:10px 12px;margin:2px 0;font-size:13px;color:"+C_TEXT+";}");
    lcl->addWidget(m_list);
    ml->addWidget(title); ml->addWidget(m_countLabel); ml->addWidget(lc, 1);
}
void HistoryTab::refresh() { m_api->getHistory(100); }
