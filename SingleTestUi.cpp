#include "singletestui.h"
#include "ui_singletestui.h"
#include <QDebug>
#include <QDateTime>
#include <QRandomGenerator>
#include <QHeaderView>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// 注意: QJsonDocument/QJsonObject/QJsonArray 头文件保留，
// 因为特殊类型发送函数中会构造这些对象传给 API

// ========== StressWorker 实现 ==========

StressWorker::StressWorker(int id, QObject* parent)
    : QObject(parent), m_id(id)
{
}

StressWorker::~StressWorker()
{
}

void StressWorker::start(int intervalMs)
{
    if (m_timer) return;

    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &StressWorker::doWork);
    m_timer->start();
}

void StressWorker::stop()
{
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    emit finished();
}

void StressWorker::doWork()
{
    SingletonAPI& api = SingletonAPI::instance();

    // 每个 worker 写入 3 个不同 key，增加测试覆盖面
    int value = QRandomGenerator::global()->bounded(1000);
    api.setValue(QString("stress/thread%1/value").arg(m_id), value);
    api.setValue(QString("stress/thread%1/temp").arg(m_id),
                 20.0 + QRandomGenerator::global()->generateDouble() * 10.0);
    api.setValue(QString("stress/thread%1/status").arg(m_id),
                 QRandomGenerator::global()->bounded(2) == 0 ? "OK" : "WARN");

    m_triggerCount.fetchAndAddRelaxed(1);
}

// ========== SingleTestUi 实现 ==========

SingleTestUi::SingleTestUi(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SingleTestUi)
    , m_api(SingletonAPI::instance())
    , m_statsTimer(new QTimer(this))
{
    ui->setupUi(this);
    setupUi();

    qRegisterMetaType<SensorData>("SensorData");
}

SingleTestUi::~SingleTestUi()
{
    m_statsTimer->stop();
    if (m_flushTimer) m_flushTimer->stop();
    flushReceivedBuffer();   // 析构前刷完剩余数据
    stopAllWorkers();

    // 取消所有订阅
    for (auto it = m_activeSubs.begin(); it != m_activeSubs.end(); ++it) {
        m_api.unsubscribe(it.value());
    }

    delete ui;
}

void SingleTestUi::setupUi()
{
    // ===== 表格初始化 =====
    ui->tableWidget_sendHistory->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget_sendHistory->setColumnCount(3);
    ui->tableWidget_sendHistory->setHorizontalHeaderLabels({"时间", "Key", "Value"});

    ui->tableWidget_received->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget_received->setColumnCount(4);
    ui->tableWidget_received->setHorizontalHeaderLabels({"时间", "Key", "Value", "匹配模式"});

    // ===== Key 下拉预设 =====
    ui->comboBox_key->setPlaceholderText(QString::fromUtf8("例: sensor/room1/temp"));
    ui->comboBox_key->addItems({
        "sensor/room1/temp",
        "sensor/room1/humi",
        "sensor/room2/temp",
        "device/motor1/status",
        "device/motor1/speed",
        "network/connection",
        "system/version",
        "custom/a/b/c/d"
    });

    // ===== 订阅 Key 下拉预设（含通配符示例）=====
    ui->comboBox_subKey->setPlaceholderText(QString::fromUtf8("例: sensor/** 或 */temp"));
    ui->comboBox_subKey->addItems({
        "sensor/**",
        "sensor/room1/*",
        "sensor/*/temp",
        "device/**",
        "*/status",
        "stress/**",
        "**",
        "system/*"
    });

    // ===== 统计定时器 =====
    m_statsTimer->setInterval(500);
    connect(m_statsTimer, &QTimer::timeout, this, &SingleTestUi::refreshStats);
    m_statsTimer->start();

    // ===== 接收缓冲批量刷新定时器 =====
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(FLUSH_INTERVAL_MS);
    connect(m_flushTimer, &QTimer::timeout, this, &SingleTestUi::flushReceivedBuffer);
    m_flushTimer->start();

    // ===== Enter 键发送 =====
    connect(ui->lineEdit_value, &QLineEdit::returnPressed,
            this, &SingleTestUi::on_pushButton_send_clicked);

    // ===== 自动创建多接收者订阅，方便测试 =====
    QStringList autoSubs = {
        "stress/**",          // 通配符: 匹配所有压力测试线程的 key
        "sensor/**",          // 通配符: 匹配所有 sensor 下的 key
        "device/**",          // 通配符: 匹配所有 device 下的 key
        "special/**",         // 通配符: 匹配所有特殊类型发送的 key
    };
    for (const QString& p : autoSubs) {
        SubscriptionId id = m_api.subscribe(p,
            [this, p](const QString& key, const QVariant& value) {
                appendReceived(key, value, p);
            }, this);
        m_activeSubs[p] = id;
        ui->listWidget_subscriptions->addItem(
            QString("%1  [ID:%2]").arg(p).arg(id));
        if (ui->comboBox_subKey->findText(p) < 0) {
            ui->comboBox_subKey->addItem(p);
        }
    }
}

// ========== 发送 ==========

void SingleTestUi::on_pushButton_send_clicked()
{
    QString key = ui->comboBox_key->currentText().trimmed();
    QString valueStr = ui->lineEdit_value->text().trimmed();

    if (key.isEmpty()) {
        ui->comboBox_key->setFocus();
        return;
    }

    QVariant value(valueStr.isEmpty() ? QVariant() : valueStr);
    bool forceNotify = ui->checkBox_forceNotify->isChecked();

    m_api.setValue(key, value, forceNotify);

    m_sendCount++;
    appendSendHistory(key, value);

    // Key 加入发送历史下拉
    if (ui->comboBox_key->findText(key) < 0) {
        ui->comboBox_key->addItem(key);
    }
}

void SingleTestUi::on_pushButton_batchSend_clicked()
{
    QMap<QString, QVariant> batch;

    // 使用当前 key 前缀批量生成
    QString baseKey = ui->comboBox_key->currentText().trimmed();
    if (baseKey.isEmpty()) return;

    int count = 5;
    for (int i = 0; i < count; ++i) {
        int val = QRandomGenerator::global()->bounded(1000);
        QString key = QString("%1/batch%2").arg(baseKey).arg(i);
        batch[key] = val;
    }

    m_api.setValues(batch);
    m_sendCount += count;

    for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
        appendSendHistory(it.key(), it.value());
    }
}

// ========== 特殊类型发送 ==========

void SingleTestUi::on_pushButton_sendJson_clicked()
{
    QJsonObject root;
    root["version"] = "2.0";
    root["deviceId"] = "DEV-20260410-001";

    QJsonObject sensors;
    sensors["temperature"] = 25.6;
    sensors["humidity"] = 62.3;
    sensors["pressure"] = 1013.25;
    root["sensors"] = sensors;

    QJsonArray alerts;
    alerts.append(QString("Low battery warning"));
    alerts.append(QString("Sensor calibration needed"));
    root["alerts"] = alerts;

    QJsonDocument doc(root);

    m_api.setValue("special/json/config", QVariant::fromValue(root), true);
    m_sendCount++;
    appendSendHistory("special/json/config", QVariant::fromValue(root));
}

void SingleTestUi::on_pushButton_sendList_clicked()
{
    QStringList items;
    items << "sensor/room1/temp" << "sensor/room1/humi" << "sensor/room2/temp"
          << "device/motor1/status" << "system/version";

    m_api.setValue("special/list/monitored_keys", items, true);
    m_sendCount++;
    appendSendHistory("special/list/monitored_keys", QVariant(items));
}

void SingleTestUi::on_pushButton_sendMap_clicked()
{
    QVariantMap config;
    config["host"] = "192.168.1.100";
    config["port"] = 8080;
    config["timeout"] = 30;
    config["reconnect"] = true;
    config["maxRetries"] = 3;
    config["protocol"] = "MQTT";

    m_api.setValue("special/map/connection_config", config, true);
    m_sendCount++;
    appendSendHistory("special/map/connection_config", QVariant::fromValue(config));
}

void SingleTestUi::on_pushButton_sendStruct_clicked()
{
    SensorData data(
        "SENSOR-A3F7",
        23.8 + QRandomGenerator::global()->generateDouble() * 5.0,
        55.0 + QRandomGenerator::global()->generateDouble() * 20.0,
        QRandomGenerator::global()->bounded(10, 100),
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
    );

    QVariant var = QVariant::fromValue(data);
    m_api.setValue("special/struct/sensor_data", var, true);
    m_sendCount++;
    appendSendHistory("special/struct/sensor_data", var);
}

void SingleTestUi::on_pushButton_sendBinary_clicked()
{
    QByteArray binary;
    binary.append(static_cast<char>(0xAA));
    binary.append(static_cast<char>(0x55));
    for (int i = 0; i < 16; ++i) {
        binary.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    binary.append(static_cast<char>(0xCC));
    binary.append(static_cast<char>(0x33));

    QString hex = binary.toHex(' ').toUpper();
    m_api.setValue("special/binary/raw_packet", binary, true);
    m_sendCount++;
    appendSendHistory("special/binary/raw_packet", QVariant(binary));
}

void SingleTestUi::on_pushButton_sendMixed_clicked()
{
    QVariantList mixed;

    QJsonObject obj;
    obj["name"] = "MotorController";
    obj["firmware"] = "v3.2.1";
    mixed.append(obj);

    QStringList tags;
    tags << "critical" << "monitored" << "auto-restart";
    mixed.append(tags);

    QVariantMap params;
    params["rpm"] = 3000;
    params["maxTemp"] = 85;
    params["enabled"] = true;
    mixed.append(params);

    mixed.append(42);
    mixed.append(3.14159);

    m_api.setValue("special/mixed/nested_data", mixed, true);
    m_sendCount++;
    appendSendHistory("special/mixed/nested_data", QVariant::fromValue(mixed));
}

// ========== 压力测试 ==========

void SingleTestUi::on_pushButton_startStress_clicked()
{
    stopAllWorkers();

    int threadCount = ui->spinBox_threadCount->value();
    int interval = ui->spinBox_interval->value();

    for (int i = 0; i < threadCount; ++i) {
        StressWorker* worker = new StressWorker(i + 1);
        QThread* thread = new QThread(this);

        worker->moveToThread(thread);
        connect(worker, &StressWorker::finished, this, [this, worker, thread]() {
            thread->quit();
            if (!thread->wait(2000)) {
                thread->terminate();
                thread->wait();
            }
            m_workers.removeOne(worker);
            m_threads.removeOne(thread);
            delete worker;
            delete thread;
        });

        connect(thread, &QThread::started, [worker, interval]() {
            worker->start(interval);
        });

        m_workers.append(worker);
        m_threads.append(thread);
        thread->start();
    }

    ui->pushButton_startStress->setEnabled(false);
    ui->pushButton_stopStress->setEnabled(true);
}

void SingleTestUi::on_pushButton_stopStress_clicked()
{
    for (auto worker : m_workers) {
        if (worker) {
            QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
        }
    }

    ui->pushButton_startStress->setEnabled(true);
    ui->pushButton_stopStress->setEnabled(false);
}

void SingleTestUi::stopAllWorkers()
{
    for (auto worker : m_workers) {
        if (worker) {
            QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
        }
    }

    for (auto thread : m_threads) {
        if (thread && thread->isRunning()) {
            thread->quit();
            if (!thread->wait(2000)) {
                thread->terminate();
                thread->wait();
            }
        }
    }

    qDeleteAll(m_workers);
    m_workers.clear();
    qDeleteAll(m_threads);
    m_threads.clear();

    ui->pushButton_startStress->setEnabled(true);
    ui->pushButton_stopStress->setEnabled(false);
}

// ========== 订阅管理 ==========

void SingleTestUi::on_pushButton_subscribe_clicked()
{
    QString pattern = ui->comboBox_subKey->currentText().trimmed();
    if (pattern.isEmpty()) return;
    if (m_activeSubs.contains(pattern)) {
        return;  // 已订阅
    }

    // 订阅，传入 this 作为 context，回调自动在 UI 线程执行
    SubscriptionId id = m_api.subscribe(pattern,
        [this, pattern](const QString& key, const QVariant& value) {
            appendReceived(key, value, pattern);   // 内部已递增 m_recvCount
        }, this);

    m_activeSubs[pattern] = id;

    // 更新列表
    ui->listWidget_subscriptions->addItem(
        QString("%1  [ID:%2]").arg(pattern).arg(id));

    // 加入下拉
    if (ui->comboBox_subKey->findText(pattern) < 0) {
        ui->comboBox_subKey->addItem(pattern);
    }
}

void SingleTestUi::on_pushButton_subscribeOnce_clicked()
{
    QString pattern = ui->comboBox_subKey->currentText().trimmed();
    if (pattern.isEmpty()) return;

    // subscribeOnce: 仅触发一次后自动取消
    SubscriptionId id = m_api.subscribeOnce(pattern,
        [this, pattern](const QString& key, const QVariant& value) {
            m_recvCount++;
            if (m_recvBuffer.size() >= MAX_BUFFER_SIZE) {
                m_recvBuffer.dequeue();
            }
            // 标记为 [ONCE] 区分普通订阅
            m_recvBuffer.enqueue({key, value, pattern + " [ONCE]"});
        }, this);

    ui->listWidget_subscriptions->addItem(
        QString("%1  [ONCE ID:%2]").arg(pattern).arg(id));

    // ONCE 类型不需要加入 m_activeSubs，因为会自动取消
}

void SingleTestUi::on_pushButton_unsubscribe_clicked()
{
    int row = ui->listWidget_subscriptions->currentRow();
    if (row < 0) return;

    QString text = ui->listWidget_subscriptions->item(row)->text();
    // 从 "[ID:xxx]" 中提取 pattern
    int bracketPos = text.lastIndexOf("  [ID:");
    if (bracketPos < 0) return;
    QString pattern = text.left(bracketPos);

    auto it = m_activeSubs.find(pattern);
    if (it != m_activeSubs.end()) {
        m_api.unsubscribe(it.value());
        m_activeSubs.erase(it);
    }

    delete ui->listWidget_subscriptions->takeItem(row);
}

void SingleTestUi::on_pushButton_clearSubs_clicked()
{
    for (auto it = m_activeSubs.begin(); it != m_activeSubs.end(); ++it) {
        m_api.unsubscribe(it.value());
    }
    m_activeSubs.clear();
    ui->listWidget_subscriptions->clear();
}

// ========== 清除日志 ==========

void SingleTestUi::on_pushButton_clearLog_clicked()
{
    //stopAllWorkers();//防止一边发送一边清除 导致崩溃
    ui->tableWidget_sendHistory->clear();
    ui->tableWidget_sendHistory->setRowCount(0);
    ui->tableWidget_received->clear();
    ui->tableWidget_received->setRowCount(0);
    m_recvBuffer.clear();   // 清空接收缓冲队列
    m_sendCount = 0;
    m_recvCount = 0;
    refreshStats();
}

// ========== 统计刷新 ==========

void SingleTestUi::refreshStats()
{
    int stressCount = 0;
    for (auto worker : m_workers) {
        if (worker) stressCount += worker->triggerCount();
    }

    ui->label_stats->setText(
        QString("订阅: %1 | 存储: %2 | 已发送: %3 | 已接收: %4")
            .arg(m_activeSubs.size())
            .arg(m_api.valueCount())
            .arg(m_sendCount)
            .arg(m_recvCount));

    ui->label_stressCount->setText(QString("已触发: %1").arg(stressCount));
}

// ========== 表格辅助 ==========

void SingleTestUi::appendSendHistory(const QString& key, const QVariant& value)
{
    QTableWidget* table = ui->tableWidget_sendHistory;

    int row = table->rowCount();
    table->insertRow(row);

    QString time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    table->setItem(row, 0, new QTableWidgetItem(time));
    table->setItem(row, 1, new QTableWidgetItem(key));
    table->setItem(row, 2, new QTableWidgetItem(SingletonAPI::formatVariant(value)));

    // 限制行数
    while (table->rowCount() > MAX_HISTORY_ROWS) {
        table->removeRow(0);
    }

    // 自动滚到底部
    table->scrollToBottom();
}

void SingleTestUi::appendReceived(const QString& key, const QVariant& value, const QString& pattern)
{
    // 仅入队，不直接操作表格 —— 由 flushReceivedBuffer 定时批量刷新
    m_recvCount++;
    if (m_recvBuffer.size() >= MAX_BUFFER_SIZE) {
        m_recvBuffer.dequeue();   // 丢弃最旧数据，防止内存无限增长
    }
    m_recvBuffer.enqueue({key, value, pattern});
}

void SingleTestUi::flushReceivedBuffer()
{
    if (m_recvBuffer.isEmpty()) return;

    QTableWidget* table = ui->tableWidget_received;
    table->setUpdatesEnabled(false);

    while (!m_recvBuffer.isEmpty()) {
        const RecvEntry& entry = m_recvBuffer.dequeue();

        int row = table->rowCount();
        table->insertRow(row);

        QString time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

        table->setItem(row, 0, new QTableWidgetItem(time));
        table->setItem(row, 1, new QTableWidgetItem(entry.key));
        table->setItem(row, 2, new QTableWidgetItem(SingletonAPI::formatVariant(entry.value)));
        table->setItem(row, 3, new QTableWidgetItem(entry.pattern));

        // 高亮通配符匹配行
        if (entry.pattern.contains('*')) {
            for (int col = 0; col < table->columnCount(); ++col) {
                QTableWidgetItem* item = table->item(row, col);
                if (item) {
                    item->setBackground(QColor(240, 248, 255));  // 淡蓝背景
                }
            }
        }
    }

    // 一次性从顶部删除超限旧行（model::removeRows 比逐行 removeRow(0) 更高效）
    int excess = table->rowCount() - MAX_RECEIVED_ROWS;
    if (excess > 0) {
        table->model()->removeRows(0, excess);
    }

    table->setUpdatesEnabled(true);
    table->scrollToBottom();
}
