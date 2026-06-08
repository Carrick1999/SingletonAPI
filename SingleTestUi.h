// MIT License
// Copyright (c) 2025 Chujh (QQ: 1206569273)
// See the LICENSE file in the project root for full license text.

#ifndef SINGLETESTUI_H
#define SINGLETESTUI_H

#include <QWidget>
#include <QThread>
#include <QTimer>
#include <QAtomicInt>
#include <QQueue>
#include "SingletonAPI.h"

namespace Ui {
class SingleTestUi;
}

/**
 * @brief 自定义结构体（测试 QVariant 封装传递）
 */
struct SensorData {
    QString sensorId;
    double temperature = 0.0;
    double humidity = 0.0;
    int batteryLevel = 100;
    QString timestamp;

    // QVariant 需要默认构造
    SensorData() = default;
    SensorData(const QString& id, double temp, double humi, int battery, const QString& ts)
        : sensorId(id), temperature(temp), humidity(humi), batteryLevel(battery), timestamp(ts) {}

    QString toString() const {
        return QString("Sensor[id=%1, temp=%.1f, humi=%.1f, bat=%2%, ts=%3]")
            .arg(sensorId).arg(temperature).arg(humidity).arg(batteryLevel).arg(timestamp);
    }
};
Q_DECLARE_METATYPE(SensorData)

/**
 * @brief 压力测试工作线程
 */
class StressWorker : public QObject
{
    Q_OBJECT
public:
    explicit StressWorker(int id, QObject* parent = nullptr);
    ~StressWorker();

    Q_INVOKABLE void start(int intervalMs);
    Q_INVOKABLE void stop();
    int triggerCount() const { return m_triggerCount.loadRelaxed(); }

signals:
    void finished();

private slots:
    void doWork();

private:
    int m_id;
    QTimer* m_timer = nullptr;
    QAtomicInt m_triggerCount{0};
};

/**
 * @brief 主测试界面 - 左侧发送 / 右侧订阅接收
 */
class SingleTestUi : public QWidget
{
    Q_OBJECT

public:
    explicit SingleTestUi(QWidget *parent = nullptr);
    ~SingleTestUi();

private slots:
    // 发送
    void on_pushButton_send_clicked();
    void on_pushButton_batchSend_clicked();

    // 特殊类型发送
    void on_pushButton_sendJson_clicked();
    void on_pushButton_sendList_clicked();
    void on_pushButton_sendMap_clicked();
    void on_pushButton_sendStruct_clicked();
    void on_pushButton_sendBinary_clicked();
    void on_pushButton_sendMixed_clicked();

    // 压力测试
    void on_pushButton_startStress_clicked();
    void on_pushButton_stopStress_clicked();

    // 订阅管理
    void on_pushButton_subscribe_clicked();
    void on_pushButton_subscribeOnce_clicked();
    void on_pushButton_unsubscribe_clicked();
    void on_pushButton_clearSubs_clicked();

    // 清除日志
    void on_pushButton_clearLog_clicked();

    // 统计刷新
    void refreshStats();

private:
    void setupUi();
    void appendSendHistory(const QString& key, const QVariant& value);
    void appendReceived(const QString& key, const QVariant& value, const QString& pattern);
    void flushReceivedBuffer();   // 批量刷新接收缓冲区到表格
    void stopAllWorkers();

    Ui::SingleTestUi *ui;
    SingletonAPI& m_api;

    // 压力测试
    QVector<StressWorker*> m_workers;
    QVector<QThread*> m_threads;

    // 统计
    QTimer* m_statsTimer;

    // 计数
    QAtomicInt m_sendCount{0};
    QAtomicInt m_recvCount{0};

    // 接收缓冲: 收到的数据先入队，定时器批量刷入表格，避免高频逐条操作导致卡死
    struct RecvEntry { QString key; QVariant value; QString pattern; };
    QQueue<RecvEntry> m_recvBuffer;
    QTimer* m_flushTimer = nullptr;
    static constexpr int FLUSH_INTERVAL_MS = 100;    // 100ms 批量刷新一次
    static constexpr int MAX_BUFFER_SIZE = 2000;     // 缓冲队列最大长度，超限直接丢弃旧数据

    // 当前活动的订阅: pattern → subscriptionId
    QMap<QString, SubscriptionId> m_activeSubs;

    // 发送历史最大行数
    static constexpr int MAX_HISTORY_ROWS = 200;
    static constexpr int MAX_RECEIVED_ROWS = 500;
};

#endif // SINGLETESTUI_H
