#ifndef HISTORYTAB_H
#define HISTORYTAB_H

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include "network/apiclient.h"

class HistoryTab : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryTab(ApiClient *api, QWidget *parent = nullptr);
    void refresh();

private:
    void setupUi();
    ApiClient *m_api;
    QListWidget *m_list;
    QLabel *m_countLabel;
};

#endif
