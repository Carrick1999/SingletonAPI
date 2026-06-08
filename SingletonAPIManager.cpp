// MIT License
// Copyright (c) 2025 Chujh (QQ: 1206569273)
// See the LICENSE file in the project root for full license text.

#include "SingletonAPIManager.h"
#include "ui_SingletonAPIManager.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QTimer>
SingletonAPIManager::SingletonAPIManager(QWidget *parent)
    : QWidget(parent), ui(new Ui::SingletonAPIManager), m_api(SingletonAPI::instance())
{
    ui->setupUi(this);

    ui->comboBox_subTypeFilter->addItem("全部", "");
    ui->comboBox_subTypeFilter->addItem("精确", "exact");
    ui->comboBox_subTypeFilter->addItem("通配符", "wildcard");
    ui->comboBox_subTypeFilter->addItem("前缀", "prefix");

    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(1000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &SingletonAPIManager::refreshAll);

    loadValues();
    loadSubscriptions();
}

SingletonAPIManager::~SingletonAPIManager()
{
    m_autoRefreshTimer->stop();
    delete ui;
}

void SingletonAPIManager::refreshAll()
{
    loadValues(ui->lineEdit_valueFilter->text().trimmed());
    loadSubscriptions();
    ui->label_stats->setText(QString("值: %1 | 订阅: %2").arg(m_api.valueCount()).arg(m_api.subscriptionCount()));
}

void SingletonAPIManager::on_pushButton_refreshAll_clicked() { refreshAll(); }

void SingletonAPIManager::on_checkBox_autoRefresh_toggled(bool checked)
{
    if (checked) m_autoRefreshTimer->start(); else m_autoRefreshTimer->stop();
}

// ========== 值管理（分页） ==========

void SingletonAPIManager::loadValues(const QString& filter)
{
    m_filteredValues.clear();
    m_api.forEachValue([&](const QString& key, const QVariant& value) {
        if (filter.isEmpty() || key.contains(filter, Qt::CaseInsensitive))
            m_filteredValues.append(qMakePair(key, value));
    });

    int pageSize = ui->spinBox_valPageSize->value();
    int total = m_filteredValues.size();
    int totalPages = qMax(1, (total + pageSize - 1) / pageSize);
    if (m_valCurrentPage > totalPages) m_valCurrentPage = totalPages;
    if (m_valCurrentPage < 1) m_valCurrentPage = 1;

    int start = (m_valCurrentPage - 1) * pageSize;
    int end = qMin(start + pageSize, total);

    ui->tableWidget_values->setRowCount(end - start);
    for (int i = start; i < end; ++i) {
        int r = i - start;
        const QVariant& val = m_filteredValues[i].second;
        ui->tableWidget_values->setItem(r, 0, new QTableWidgetItem(m_filteredValues[i].first));
        ui->tableWidget_values->setItem(r, 1, new QTableWidgetItem(val.typeName()));
        ui->tableWidget_values->setItem(r, 2, new QTableWidgetItem(SingletonAPI::formatVariant(val)));
    }

    ui->label_valPageInfo->setText(QString("第 %1/%2 页 (共 %3 条)").arg(m_valCurrentPage).arg(totalPages).arg(total));
    ui->pushButton_valPrevPage->setEnabled(m_valCurrentPage > 1);
    ui->pushButton_valNextPage->setEnabled(m_valCurrentPage < totalPages);
}

void SingletonAPIManager::on_lineEdit_valueFilter_textChanged(const QString&)
{
    m_valCurrentPage = 1;
    loadValues(ui->lineEdit_valueFilter->text().trimmed());
}

void SingletonAPIManager::on_pushButton_valPrevPage_clicked() { --m_valCurrentPage; loadValues(ui->lineEdit_valueFilter->text().trimmed()); }
void SingletonAPIManager::on_pushButton_valNextPage_clicked() { ++m_valCurrentPage; loadValues(ui->lineEdit_valueFilter->text().trimmed()); }

void SingletonAPIManager::on_spinBox_valPageSize_valueChanged(int)
{
    m_valCurrentPage = 1;
    loadValues(ui->lineEdit_valueFilter->text().trimmed());
}

void SingletonAPIManager::on_pushButton_addValue_clicked()
{
    QString key = ui->lineEdit_newKey->text().trimmed();
    QString val = ui->lineEdit_newValue->text().trimmed();
    if (key.isEmpty()) { ui->lineEdit_newKey->setFocus(); return; }
    m_api.setValue(key, val.isEmpty() ? QVariant() : val, true);
    loadValues(ui->lineEdit_valueFilter->text().trimmed());
    ui->lineEdit_newKey->clear();
    ui->lineEdit_newValue->clear();
}

void SingletonAPIManager::on_pushButton_updateValue_clicked()
{
    int row = ui->tableWidget_values->currentRow();
    if (row < 0) return;
    auto* k = ui->tableWidget_values->item(row, 0);
    auto* v = ui->tableWidget_values->item(row, 2);
    if (!k || !v) return;
    m_api.setValue(k->text(), v->text(), true);
    loadValues(ui->lineEdit_valueFilter->text().trimmed());
}

void SingletonAPIManager::on_pushButton_deleteValue_clicked()
{
    int row = ui->tableWidget_values->currentRow();
    if (row < 0) return;
    auto* k = ui->tableWidget_values->item(row, 0);
    if (!k) return;
    m_api.remove(k->text());
    loadValues(ui->lineEdit_valueFilter->text().trimmed());
}

void SingletonAPIManager::on_pushButton_clearValues_clicked()
{
    if (QMessageBox::question(this, "确认", "确定清空所有存储的值？") == QMessageBox::Yes) {
        m_api.clear();
        loadValues();
    }
}

void SingletonAPIManager::on_tableWidget_values_cellDoubleClicked(int row, int column)
{
    if (auto* item = ui->tableWidget_values->item(row, column))
        ui->tableWidget_values->editItem(item);
}

// ========== 订阅管理（分页+搜索） ==========

void SingletonAPIManager::loadSubscriptions()
{
    QString patternFilter = ui->lineEdit_subFilter->text().trimmed();
    QString typeFilter = ui->comboBox_subTypeFilter->currentData().toString();
    m_filteredSubs = m_api.searchSubscriptions(patternFilter, typeFilter);

    int pageSize = ui->spinBox_subPageSize->value();
    int total = m_filteredSubs.size();
    int totalPages = qMax(1, (total + pageSize - 1) / pageSize);
    if (m_subCurrentPage > totalPages) m_subCurrentPage = totalPages;
    if (m_subCurrentPage < 1) m_subCurrentPage = 1;

    int start = (m_subCurrentPage - 1) * pageSize;
    int end = qMin(start + pageSize, total);

    // 只填充当前页
    ui->tableWidget_subs->setRowCount(end - start);
    for (int i = start; i < end; ++i) {
        const auto& info = m_filteredSubs[i];
        int r = i - start;
        ui->tableWidget_subs->setItem(r, 0, new QTableWidgetItem(QString::number(info.id)));
        ui->tableWidget_subs->setItem(r, 1, new QTableWidgetItem(info.pattern));

        QString type = info.isPrefix ? "前缀" : (info.isWildcard ? "通配符" : "精确");
        ui->tableWidget_subs->setItem(r, 2, new QTableWidgetItem(type));
        ui->tableWidget_subs->setItem(r, 3, new QTableWidgetItem(info.contextName.isEmpty() ? "-" : info.contextName));
        ui->tableWidget_subs->setItem(r, 4, new QTableWidgetItem(info.threadName.isEmpty() ? "-" : info.threadName));
        ui->tableWidget_subs->setItem(r, 5, new QTableWidgetItem(info.ownerName.isEmpty() ? "-" : info.ownerName));
    }

    ui->label_subPageInfo->setText(QString("第 %1/%2 页 (共 %3 条)").arg(m_subCurrentPage).arg(totalPages).arg(total));
    ui->pushButton_subPrevPage->setEnabled(m_subCurrentPage > 1);
    ui->pushButton_subNextPage->setEnabled(m_subCurrentPage < totalPages);
}

void SingletonAPIManager::on_lineEdit_subFilter_textChanged(const QString&)
{
    m_subCurrentPage = 1;
    loadSubscriptions();
}

void SingletonAPIManager::on_comboBox_subTypeFilter_currentTextChanged(const QString&)
{
    m_subCurrentPage = 1;
    loadSubscriptions();
}

void SingletonAPIManager::on_pushButton_subPrevPage_clicked() { --m_subCurrentPage; loadSubscriptions(); }
void SingletonAPIManager::on_pushButton_subNextPage_clicked() { ++m_subCurrentPage; loadSubscriptions(); }

void SingletonAPIManager::on_spinBox_subPageSize_valueChanged(int)
{
    m_subCurrentPage = 1;
    loadSubscriptions();
}

void SingletonAPIManager::on_pushButton_disconnectSub_clicked()
{
    int row = ui->tableWidget_subs->currentRow();
    if (row < 0) return;
    auto* idItem = ui->tableWidget_subs->item(row, 0);
    if (!idItem) return;

    int pageSize = ui->spinBox_subPageSize->value();
    int start = (m_subCurrentPage - 1) * pageSize;
    int idx = start + row;
    if (idx < m_filteredSubs.size()) {
        m_api.unsubscribe(m_filteredSubs[idx].id);
        loadSubscriptions();
    }
}

void SingletonAPIManager::on_pushButton_forceTriggerSub_clicked()
{
    int row = ui->tableWidget_subs->currentRow();
    if (row < 0) return;

    int pageSize = ui->spinBox_subPageSize->value();
    int start = (m_subCurrentPage - 1) * pageSize;
    int idx = start + row;
    if (idx >= m_filteredSubs.size()) return;

    QString key = ui->lineEdit_ftKey->text().trimmed();
    QString val = ui->lineEdit_ftValue->text().trimmed();
    if (key.isEmpty()) { ui->lineEdit_ftKey->setFocus(); return; }

    if (!m_api.forceTrigger(m_filteredSubs[idx].id, key, val.isEmpty() ? QVariant() : val))
        QMessageBox::warning(this, "触发失败", "订阅不存在或已过期");
}

void SingletonAPIManager::on_pushButton_disconnectAllSubs_clicked()
{
    if (QMessageBox::question(this, "确认", "确定断开所有订阅？\n这会影响所有监听模块！") == QMessageBox::Yes) {
        for (const auto& info : m_filteredSubs)
            m_api.unsubscribe(info.id);
        loadSubscriptions();
    }
}

void SingletonAPIManager::on_tableWidget_subs_cellDoubleClicked(int row, int column)
{
    if (column == 1) {
        int pageSize = ui->spinBox_subPageSize->value();
        int start = (m_subCurrentPage - 1) * pageSize;
        int idx = start + row;
        if (idx < m_filteredSubs.size()) {
            QString p = m_filteredSubs[idx].pattern;
            p.replace("**", "test/example");
            p.replace("*", "test");
            ui->lineEdit_ftKey->setText(p);
        }
    }
}
