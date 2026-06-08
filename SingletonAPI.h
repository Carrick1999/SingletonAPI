/**
 * @file SingletonAPI.h
 * @brief 轻量级线程安全可观察 Key-Value 存储 + 事件总线
 *
 * 核心设计：
 *   setValue(key, value)    → 存储值并自动通知所有订阅者
 *   subscribe(key, cb)      → Lambda 或 Qt 槽订阅，支持通配符 "*"
 *   getValue<T>(key)        → 类型安全读取
 *   valueChanged 信号       → Qt AutoConnection 自动跨线程
 *
 * 特性：
 *   - 通配符匹配：subscribe("sensor/\*", cb) 可匹配 "sensor/temp", "sensor/humi" ...
 *   - 泛型读取：getValue<int>("count"), getValue<QString>("name")
 *   - 批量更新：setValues(map) 只触发一次通知
 *   - 变更过滤：值相同则不通知（可关闭）
 *   - 自动跨线程：Lambda 订阅支持指定 context 对象，回调在 context 线程执行
 *   - QObject 订阅者自动清理：receiver 被销毁时自动移除订阅
 *
 * @version 5.0
 * @date 2026
 */

#ifndef SINGLETONAPI_H
#define SINGLETONAPI_H

#include <QObject>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <QMutex>
#include <QPointer>
#include <QAtomicInteger>
#include <type_traits>

class QTimer;  // forward declaration

// ========== DLL 导出/导入宏 ==========
#ifdef SINGLETONAPI_LIBRARY
#  define SINGLETON_API Q_DECL_EXPORT
#elif defined(SINGLETONAPI_USE_LIB)
#  define SINGLETON_API Q_DECL_IMPORT
#else
#  define SINGLETON_API
#endif

using SubscriptionId = quint64;

/**
 * @brief 值变化回调签名
 */
using ValueCallback = std::function<void(const QString& key, const QVariant& value)>;

/**
 * @brief 订阅信息（用于管理界面展示）
 */
struct SINGLETON_API SubscriptionInfo {
    SubscriptionId id = 0;
    QString pattern;
    bool isWildcard = false;
    bool isPrefix = false;
    QString contextName;
    QString ownerName;
    QString threadName;
};

/**
 * @brief 变更记录（用于变更日志功能）
 */
struct SINGLETON_API ChangeRecord {
    QString key;
    QVariant oldValue;
    QVariant newValue;
    qint64 timestamp = 0;
};

class SINGLETON_API SingletonAPI : public QObject
{
    Q_OBJECT

public:
    static SingletonAPI& instance();

    /**
     * @brief 显式销毁单例内部资源（在 QApplication 销毁前调用，避免静态析构顺序问题）
     */
    static void shutdown();

    SingletonAPI(const SingletonAPI&) = delete;
    SingletonAPI& operator=(const SingletonAPI&) = delete;

    // ========== 核心读写 ==========
    //还是默认强通知吧
    /**
     * @brief 设置值并通知订阅者
     * @return true 表示值确实变化了（或强制通知了），false 表示值未变且未强制
     */
    bool setValue(const QString& key, const QVariant& value, bool forceNotify = true);

    /**
     * @brief 批量设置值，合并为一次通知
     * @return 实际发生变化的 key 数量
     */
    int setValues(const QMap<QString, QVariant>& values, bool forceNotify = true);

    /**
     * @brief 类型安全读取
     */
    template<typename T>
    T getValue(const QString& key, const T& defaultValue = T()) const {
        QVariant v = getValue(key);
        if (v.isValid()) {
            if (std::is_same<T, QString>::value) {
                return v.toString();
            } else if (std::is_same<T, int>::value) {
                return v.toInt();
            } else if (std::is_same<T, double>::value) {
                return v.toDouble();
            } else if (std::is_same<T, bool>::value) {
                return v.toBool();
            } else if (std::is_same<T, float>::value) {
                return static_cast<float>(v.toDouble());
            } else if (std::is_same<T, QStringList>::value) {
                return v.toStringList();
            } else {
                return v.value<T>();
            }
        }
        return defaultValue;
    }

    QVariant getValue(const QString& key) const;

    // ========== 批量查询 ==========

    /** @brief 按键列表批量获取值 */
    QMap<QString, QVariant> getValues(const QStringList& keys) const;

    /** @brief 获取所有匹配指定前缀的键 */
    QStringList keysByPrefix(const QString& prefix) const;

    /**
     * @brief 按前缀批量删除
     * @param notify 是否通知订阅者（通知时值为 QVariant()）
     * @return 删除的数量
     */
    int removeByPrefix(const QString& prefix, bool notify = false);

    /**
     * @brief 遍历所有键值对（零拷贝，在锁内直接回调）
     */
    void forEachValue(std::function<void(const QString&, const QVariant&)> visitor) const;

    // ========== 订阅管理 ==========

    /**
     * @brief Lambda 订阅（工作线程安全）
     * @param key 键或通配符模式
     * @param callback 回调函数
     * @param context 上下文对象（可选）
     */
    SubscriptionId subscribe(const QString& key, ValueCallback callback, QObject* context = nullptr);

    /**
     * @brief Lambda 订阅（按前缀匹配）
     */
    SubscriptionId subscribeByPrefix(const QString& prefix, ValueCallback callback, QObject* context = nullptr);

    /**
     * @brief 一次性订阅：触发一次后自动取消
     */
    SubscriptionId subscribeOnce(const QString& key, ValueCallback callback, QObject* context = nullptr);

    bool unsubscribe(SubscriptionId id);
    int unsubscribeByKey(const QString& key);
    int unsubscribeByOwner(QObject* owner);

    // ========== 便捷查询 ==========

    bool contains(const QString& key) const;
    QStringList keys() const;

    /**
     * @brief 删除指定键
     * @param notify 是否通知订阅者值被移除
     */
    bool remove(const QString& key, bool notify = false);

    /**
     * @brief 清空所有值
     * @param notify 是否通知订阅者所有值被移除
     */
    void clear(bool notify = false);

    // ========== 统计 ==========

    int subscriptionCount() const;
    int subscriptionCount(const QString& key) const;
    int valueCount() const;
    QString statistics() const;

    // ========== 管理接口 ==========

    QList<SubscriptionInfo> allSubscriptions() const;

    QList<SubscriptionInfo> searchSubscriptions(
        const QString& patternFilter = QString(),
        const QString& typeFilter = QString(),
        const QString& contextFilter = QString()) const;

    QMap<QString, QVariant> allValues() const;

    bool forceTrigger(SubscriptionId id, const QString& key, const QVariant& value);

    // ========== 变更日志 ==========

    /** @brief 启用变更日志（每个 key 最多保留 maxEntries 条记录） */
    void enableChangeLog(int maxEntries = 1000);

    /** @brief 禁用变更日志并清空历史 */
    void disableChangeLog();

    /** @brief 获取指定 key 的变更历史 */
    QList<ChangeRecord> changeHistory(const QString& key) const;

    /** @brief 获取所有 key 的变更历史 */
    QList<ChangeRecord> changeHistory() const;

    // ========== 防抖 ==========

    /**
     * @brief 防抖设置值：短时间内多次调用只触发最后一次
     * @param delayMs 防抖延迟（毫秒），线程安全
     */
    void setValueDebounced(const QString& key, const QVariant& value, int delayMs = 100);

    // ========== 工具方法 ==========

    static QString formatVariant(const QVariant& v);

signals:
    void valueChanged(const QString& key, const QVariant& value);

private:
    explicit SingletonAPI(QObject* parent = nullptr);
    ~SingletonAPI();

    struct Subscription {
        SubscriptionId id = 0;
        ValueCallback callback;
        QPointer<QObject> context;
        QPointer<QObject> owner;
        QString pattern;
        bool isWildcard = false;
        bool isPrefix = false;
        bool isOnce = false;
        mutable QStringList cachedPatternParts;  // 缓存 pattern 分段，避免重复 split

        bool isExpired() const { return !callback && context.isNull(); }
    };

    // 内部订阅实现（消除 subscribe/subscribeByPrefix 重复代码）
    SubscriptionId subscribeInternal(const QString& pattern, bool isWildcard, bool isPrefix, bool isOnce,
                                     ValueCallback callback, QObject* context);

    // 辅助方法（消除 allSubscriptions/searchSubscriptions 重复代码）
    SubscriptionInfo toInfo(const Subscription& sub) const;

    void notifySubscribers(const QString& key, const QVariant& value);
    bool matchPattern(const QString& pattern, const QString& key) const;
    bool matchPatternCached(const Subscription& sub, const QString& key) const;
    QList<Subscription> getMatchingSubscriptions(const QString& key) const;
    void recordChange(const QString& key, const QVariant& oldValue, const QVariant& newValue);
    void cleanup();

    static QString extractWildcardPrefix(const QString& pattern);

    mutable QMutex m_mutex;
    QMap<QString, QVariant> m_values;
    QMap<SubscriptionId, Subscription> m_subscriptions;
    QMultiMap<QString, SubscriptionId> m_keyToSubIds;
    QSet<SubscriptionId> m_wildcardSubs;                         // QSet: O(1) 查找/删除
    QHash<QString, QList<SubscriptionId>> m_prefixIndex;          // 通配符前缀索引
    QAtomicInteger<SubscriptionId> m_nextId{1};

    // 变更日志
    bool m_changeLogEnabled = false;
    int m_maxChangeLog = 1000;
    QMap<QString, QList<ChangeRecord>> m_changeLog;
    mutable QMutex m_changeLogMutex;

    // 防抖定时器
    QHash<QString, QTimer*> m_debounceTimers;
};

#endif // SINGLETONAPI_H
