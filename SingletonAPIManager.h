// MIT License
// Copyright (c) 2025 Chujh (QQ: 1206569273)
// See the LICENSE file in the project root for full license text.

#ifndef SINGLETONAPIMANAGER_H
#define SINGLETONAPIMANAGER_H

#include <QWidget>
#include "SingletonAPI.h"

#ifdef SINGLETONAPI_LIBRARY
#  define MANAGER_API Q_DECL_EXPORT
#elif defined(SINGLETONAPI_USE_LIB)
#  define MANAGER_API Q_DECL_IMPORT
#else
#  define MANAGER_API
#endif

namespace Ui {
class SingletonAPIManager;
}

class MANAGER_API SingletonAPIManager : public QWidget
{
    Q_OBJECT

public:
    explicit SingletonAPIManager(QWidget *parent = nullptr);
    ~SingletonAPIManager();

public slots:
    void refreshAll();

private slots:
    void on_pushButton_refreshAll_clicked();
    void on_checkBox_autoRefresh_toggled(bool checked);

    // 值 Tab
    void on_lineEdit_valueFilter_textChanged(const QString& text);
    void on_pushButton_valPrevPage_clicked();
    void on_pushButton_valNextPage_clicked();
    void on_spinBox_valPageSize_valueChanged(int val);
    void on_pushButton_addValue_clicked();
    void on_pushButton_updateValue_clicked();
    void on_pushButton_deleteValue_clicked();
    void on_pushButton_clearValues_clicked();
    void on_tableWidget_values_cellDoubleClicked(int row, int column);

    // 订阅 Tab
    void on_lineEdit_subFilter_textChanged(const QString& text);
    void on_comboBox_subTypeFilter_currentTextChanged(const QString& text);
    void on_pushButton_subPrevPage_clicked();
    void on_pushButton_subNextPage_clicked();
    void on_spinBox_subPageSize_valueChanged(int val);
    void on_pushButton_disconnectSub_clicked();
    void on_pushButton_forceTriggerSub_clicked();
    void on_pushButton_disconnectAllSubs_clicked();
    void on_tableWidget_subs_cellDoubleClicked(int row, int column);

private:
    void loadValues(const QString& filter = QString());
    void loadSubscriptions();

    Ui::SingletonAPIManager *ui;
    SingletonAPI& m_api;

    QTimer* m_autoRefreshTimer = nullptr;

    // 分页缓存
    QList<SubscriptionInfo> m_filteredSubs;
    int m_subCurrentPage = 1;

    QList<QPair<QString, QVariant>> m_filteredValues;
    int m_valCurrentPage = 1;
};

#endif // SINGLETONAPIMANAGER_H
