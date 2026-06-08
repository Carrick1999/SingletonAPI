/**
 * @file SingletonAPI.cpp
 * @brief 轻量级线程安全可观察 Key-Value 存储 + 事件总线
 * @version 5.0
 */

#include "SingletonAPI.h"
#include <QThread>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDateTime>

// ========== 单例 ==========

SingletonAPI::SingletonAPI(QObject* parent)
    : QObject(parent)
{
}

SingletonAPI::~SingletonAPI()
{
    // 清理防抖定时器
    for (auto it = m_debounceTimers.begin(); it != m_debounceTimers.end(); ++it) {
        it.value()->stop();
        delete it.value();
    }
    m_debounceTimers.clear();

    QMutexLocker locker(&m_mutex);
    m_subscriptions.clear();
    m_values.clear();
}

SingletonAPI& SingletonAPI::instance()
{
    static SingletonAPI inst;
    return inst;
}

void SingletonAPI::shutdown()
{
    instance().cleanup();
}

void SingletonAPI::cleanup()
{
    // 停止并删除防抖定时器
    for (auto it = m_debounceTimers.begin(); it != m_debounceTimers.end(); ++it) {
        it.value()->stop();
        delete it.value();
    }
    m_debounceTimers.clear();

    // 清空核心数据
    {
        QMutexLocker locker(&m_mutex);
        m_subscriptions.clear();
        m_values.clear();
        m_wildcardSubs.clear();
        m_prefixIndex.clear();
        m_keyToSubIds.clear();
    }

    {
        QMutexLocker locker(&m_changeLogMutex);
        m_changeLog.clear();
    }
}

// ========== 核心读写 ==========

bool SingletonAPI::setValue(const QString& key, const QVariant& value, bool forceNotify)
{
    QVariant oldValue;
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        oldValue = m_values.value(key);
        if (forceNotify || !m_values.contains(key) || oldValue != value) {
            m_values[key] = value;
            changed = true;
        }
    }

    if (changed) {
        recordChange(key, oldValue, value);
        emit valueChanged(key, value);
        notifySubscribers(key, value);
    }

    return changed;
}

int SingletonAPI::setValues(const QMap<QString, QVariant>& values,bool forceNotify)
{
    if (values.isEmpty()) return 0;

    QStringList changedKeys;
    QMap<QString, QVariant> oldValues;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (!m_values.contains(it.key()) || m_values[it.key()] != it.value() || forceNotify) {
                oldValues[it.key()] = m_values.value(it.key());
                m_values[it.key()] = it.value();
                changedKeys.append(it.key());
            }
        }
    }

    for (const QString& key : changedKeys) {
        recordChange(key, oldValues.value(key), values.value(key));
        const QVariant& val = values.value(key);
        emit valueChanged(key, val);
        notifySubscribers(key, val);
    }

    return changedKeys.size();
}

QVariant SingletonAPI::getValue(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    return m_values.value(key, QVariant());
}

// ========== 批量查询 ==========

QMap<QString, QVariant> SingletonAPI::getValues(const QStringList& keys) const
{
    QMutexLocker locker(&m_mutex);
    QMap<QString, QVariant> result;
    for (const QString& key : keys) {
        if (m_values.contains(key)) {
            result.insert(key, m_values.value(key));
        }
    }
    return result;
}

QStringList SingletonAPI::keysByPrefix(const QString& prefix) const
{
    QMutexLocker locker(&m_mutex);
    QStringList result;
    auto it = m_values.lowerBound(prefix);
    while (it != m_values.end() && it.key().startsWith(prefix)) {
        result.append(it.key());
        ++it;
    }
    return result;
}

int SingletonAPI::removeByPrefix(const QString& prefix, bool notify)
{
    QMap<QString, QVariant> removed;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_values.lowerBound(prefix);
        while (it != m_values.end() && it.key().startsWith(prefix)) {
            removed.insert(it.key(), it.value());
            it = m_values.erase(it);
        }
    }

    if (notify) {
        for (auto it = removed.constBegin(); it != removed.constEnd(); ++it) {
            recordChange(it.key(), it.value(), QVariant());
            emit valueChanged(it.key(), QVariant());
            notifySubscribers(it.key(), QVariant());
        }
    }

    return removed.size();
}

void SingletonAPI::forEachValue(std::function<void(const QString&, const QVariant&)> visitor) const
{
    QMutexLocker locker(&m_mutex);
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        visitor(it.key(), it.value());
    }
}

// ========== 订阅管理 ==========

SubscriptionId SingletonAPI::subscribeInternal(const QString& pattern, bool isWildcard,
                                               bool isPrefix, bool isOnce,
                                               ValueCallback callback, QObject* context)
{
    QMutexLocker locker(&m_mutex);

    SubscriptionId id = m_nextId.fetchAndAddRelaxed(1);
    Subscription sub;
    sub.id = id;
    sub.callback = std::move(callback);
    sub.context = context;
    sub.owner = context;
    sub.pattern = pattern;
    sub.isWildcard = isWildcard;
    sub.isPrefix = isPrefix;
    sub.isOnce = isOnce;

    m_subscriptions[id] = sub;

    if (isWildcard) {
        m_wildcardSubs.insert(id);
        QString prefix = extractWildcardPrefix(pattern);
        if (!prefix.isEmpty()) {
            m_prefixIndex[prefix].append(id);
        }
    } else {
        m_keyToSubIds.insert(pattern, id);
    }

    return id;
}

SubscriptionId SingletonAPI::subscribe(const QString& key, ValueCallback callback, QObject* context)
{
    return subscribeInternal(key, key.contains('*'), false, false, std::move(callback), context);
}

SubscriptionId SingletonAPI::subscribeByPrefix(const QString& prefix, ValueCallback callback, QObject* context)
{
    return subscribeInternal(prefix, true, true, false, std::move(callback), context);
}

SubscriptionId SingletonAPI::subscribeOnce(const QString& key, ValueCallback callback, QObject* context)
{
    auto idPtr = std::make_shared<SubscriptionId>(0);
    auto wrapper = [this, idPtr, cb = std::move(callback)](const QString& k, const QVariant& v) mutable {
        cb(k, v);
        if (*idPtr != 0) {
            unsubscribe(*idPtr);
            *idPtr = 0;  // 防止重复取消
        }
    };

    bool isWildcard = key.contains('*');
    SubscriptionId id = subscribeInternal(key, isWildcard, false, true, std::move(wrapper), context);
    *idPtr = id;
    return id;
}

bool SingletonAPI::unsubscribe(SubscriptionId id)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return false;

    if (!it->isWildcard) {
        m_keyToSubIds.remove(it->pattern, id);
    } else {
        m_wildcardSubs.remove(id);
        QString prefix = extractWildcardPrefix(it->pattern);
        if (!prefix.isEmpty()) {
            m_prefixIndex[prefix].removeOne(id);
            if (m_prefixIndex[prefix].isEmpty()) {
                m_prefixIndex.remove(prefix);
            }
        }
    }

    m_subscriptions.erase(it);
    return true;
}

int SingletonAPI::unsubscribeByKey(const QString& key)
{
    QMutexLocker locker(&m_mutex);

    // 精确匹配的订阅
    QList<SubscriptionId> exactIds = m_keyToSubIds.values(key);
    int count = 0;

    for (SubscriptionId id : exactIds) {
        m_subscriptions.remove(id);
        m_keyToSubIds.remove(key, id);
        ++count;
    }

    // 通配符订阅中模式文本完全匹配的
    QList<SubscriptionId> toRemoveWildcard;
    for (SubscriptionId id : m_wildcardSubs) {
        auto it = m_subscriptions.find(id);
        if (it != m_subscriptions.end() && it->pattern == key) {
            toRemoveWildcard.append(id);
            m_subscriptions.erase(it);
            ++count;
        }
    }
    for (SubscriptionId id : toRemoveWildcard) {
        m_wildcardSubs.remove(id);
        QString prefix = extractWildcardPrefix(key);
        if (!prefix.isEmpty()) {
            m_prefixIndex[prefix].removeOne(id);
            if (m_prefixIndex[prefix].isEmpty()) {
                m_prefixIndex.remove(prefix);
            }
        }
    }

    return count;
}

int SingletonAPI::unsubscribeByOwner(QObject* owner)
{
    if (!owner) return 0;

    QMutexLocker locker(&m_mutex);
    QList<SubscriptionId> toRemove;

    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        if (it->owner == owner) {
            toRemove.append(it.key());
        }
    }

    for (SubscriptionId id : toRemove) {
        auto it = m_subscriptions.find(id);
        if (!it->isWildcard) {
            m_keyToSubIds.remove(it->pattern, id);
        } else {
            m_wildcardSubs.remove(id);
            QString prefix = extractWildcardPrefix(it->pattern);
            if (!prefix.isEmpty()) {
                m_prefixIndex[prefix].removeOne(id);
                if (m_prefixIndex[prefix].isEmpty()) {
                    m_prefixIndex.remove(prefix);
                }
            }
        }
        m_subscriptions.erase(it);
    }

    return toRemove.size();
}

// ========== 便捷查询 ==========

bool SingletonAPI::contains(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    return m_values.contains(key);
}

QStringList SingletonAPI::keys() const
{
    QMutexLocker locker(&m_mutex);
    return m_values.keys();
}

bool SingletonAPI::remove(const QString& key, bool notify)
{
    QVariant oldValue;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_values.contains(key)) return false;
        oldValue = m_values.value(key);
        m_values.remove(key);
    }

    if (notify) {
        recordChange(key, oldValue, QVariant());
        emit valueChanged(key, QVariant());
        notifySubscribers(key, QVariant());
    }

    return true;
}

void SingletonAPI::clear(bool notify)
{
    QMap<QString, QVariant> oldValues;
    {
        QMutexLocker locker(&m_mutex);
        oldValues = m_values;
        m_values.clear();
    }

    if (notify) {
        for (auto it = oldValues.constBegin(); it != oldValues.constEnd(); ++it) {
            recordChange(it.key(), it.value(), QVariant());
            emit valueChanged(it.key(), QVariant());
            notifySubscribers(it.key(), QVariant());
        }
    }
}

// ========== 统计 ==========

int SingletonAPI::subscriptionCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_subscriptions.size();
}

int SingletonAPI::subscriptionCount(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    return m_keyToSubIds.count(key);
}

int SingletonAPI::valueCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_values.size();
}

QString SingletonAPI::statistics() const
{
    QMutexLocker locker(&m_mutex);

    return QString(
        "\n=== SingletonAPI Statistics ===\n"
        "  Subscriptions: %1 (wildcard: %2)\n"
        "  Tracked Keys:  %3\n"
        "  Stored Values: %4\n"
        "  Prefix Index:  %5 entries\n"
        "  Change Log:    %6\n"
        "==============================\n"
    ).arg(m_subscriptions.size())
     .arg(m_wildcardSubs.size())
     .arg(m_keyToSubIds.uniqueKeys().size())
     .arg(m_values.size())
     .arg(m_prefixIndex.size())
     .arg(m_changeLogEnabled ? "enabled" : "disabled");
}

// ========== 管理接口 ==========

QList<SubscriptionInfo> SingletonAPI::allSubscriptions() const
{
    QMutexLocker locker(&m_mutex);

    QList<SubscriptionInfo> result;
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
        if (it->isExpired()) continue;
        result.append(toInfo(*it));
    }
    return result;
}

QMap<QString, QVariant> SingletonAPI::allValues() const
{
    QMutexLocker locker(&m_mutex);
    return m_values;
}

bool SingletonAPI::forceTrigger(SubscriptionId id, const QString& key, const QVariant& value)
{
    QMutexLocker locker(&m_mutex);

    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end() || it->isExpired()) return false;

    const Subscription sub = *it;
    locker.unlock();

    if (!sub.callback) return false;

    if (sub.context) {
        QObject* ctx = sub.context.data();
        if (!ctx) return false;

        if (QThread::currentThread() == ctx->thread()) {
            sub.callback(key, value);
        } else {
            // QPointer guard 防止 context 在 lambda 执行前销毁
            QPointer<QObject> guard(ctx);
            QMetaObject::invokeMethod(ctx, [sub, key, value, guard]() {
                if (!guard) return;
                sub.callback(key, value);
            }, Qt::QueuedConnection);
        }
    } else {
        sub.callback(key, value);
    }
    return true;
}

QList<SubscriptionInfo> SingletonAPI::searchSubscriptions(
    const QString& patternFilter, const QString& typeFilter, const QString& contextFilter) const
{
    QMutexLocker locker(&m_mutex);

    QList<SubscriptionInfo> result;
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
        if (it->isExpired()) continue;

        SubscriptionInfo info = toInfo(*it);

        // 类型过滤
        if (!typeFilter.isEmpty()) {
            if (typeFilter == "wildcard" && !info.isWildcard) continue;
            if (typeFilter == "prefix" && !info.isPrefix) continue;
            if (typeFilter == "exact" && info.isWildcard) continue;
        }

        // pattern 模糊匹配
        if (!patternFilter.isEmpty() && !info.pattern.contains(patternFilter, Qt::CaseInsensitive))
            continue;

        // context 模糊匹配
        if (!contextFilter.isEmpty() && !info.contextName.contains(contextFilter, Qt::CaseInsensitive))
            continue;

        result.append(info);
    }
    return result;
}

// ========== 变更日志 ==========

void SingletonAPI::enableChangeLog(int maxEntries)
{
    m_maxChangeLog = maxEntries > 0 ? maxEntries : 1000;
    m_changeLogEnabled = true;
}

void SingletonAPI::disableChangeLog()
{
    m_changeLogEnabled = false;
    QMutexLocker locker(&m_changeLogMutex);
    m_changeLog.clear();
}

void SingletonAPI::recordChange(const QString& key, const QVariant& oldValue, const QVariant& newValue)
{
    if (!m_changeLogEnabled) return;

    QMutexLocker locker(&m_changeLogMutex);

    ChangeRecord record;
    record.key = key;
    record.oldValue = oldValue;
    record.newValue = newValue;
    record.timestamp = QDateTime::currentMSecsSinceEpoch();

    auto& list = m_changeLog[key];
    list.append(record);

    while (list.size() > m_maxChangeLog) {
        list.removeFirst();
    }
}

QList<ChangeRecord> SingletonAPI::changeHistory(const QString& key) const
{
    QMutexLocker locker(&m_changeLogMutex);
    return m_changeLog.value(key);
}

QList<ChangeRecord> SingletonAPI::changeHistory() const
{
    QMutexLocker locker(&m_changeLogMutex);
    QList<ChangeRecord> result;
    for (auto it = m_changeLog.constBegin(); it != m_changeLog.constEnd(); ++it) {
        result.append(it.value());
    }
    return result;
}

// ========== 防抖 ==========

void SingletonAPI::setValueDebounced(const QString& key, const QVariant& value, int delayMs)
{
    // 通过 QueuedConnection 确保定时器操作在 SingletonAPI 所在线程执行
    QMetaObject::invokeMethod(this, [this, key, value, delayMs]() {
        auto it = m_debounceTimers.find(key);
        if (it != m_debounceTimers.end()) {
            // 已有定时器：更新待发送值并重置延迟
            it.value()->setProperty("_pendingValue", value);
            it.value()->start(delayMs);
        } else {
            // 创建新定时器
            QTimer* timer = new QTimer(this);
            timer->setSingleShot(true);
            timer->setProperty("_pendingValue", value);
            timer->setProperty("_debounceKey", key);
            connect(timer, &QTimer::timeout, this, [this, timer]() {
                QString k = timer->property("_debounceKey").toString();
                QVariant v = timer->property("_pendingValue");
                setValue(k, v);
                m_debounceTimers.remove(k);
                timer->deleteLater();
            });
            timer->start(delayMs);
            m_debounceTimers[key] = timer;
        }
    }, Qt::QueuedConnection);
}

// ========== 内部方法 ==========

/**
 * @brief 通用路径模式匹配（支持任意位置、任意层级的通配符）
 */
static bool matchSegments(const QStringList& pat, int pi,
                          const QStringList& key, int ki)
{
    while (pi < pat.size()) {
        const QString& seg = pat[pi];

        if (seg == "**") {
            for (int i = ki; i <= key.size(); ++i) {
                if (matchSegments(pat, pi + 1, key, i))
                    return true;
            }
            return false;
        }

        if (ki >= key.size())
            return false;

        if (seg == "*") {
            ++pi;
            ++ki;
            continue;
        }

        if (seg != key[ki])
            return false;

        ++pi;
        ++ki;
    }

    return ki == key.size();
}

bool SingletonAPI::matchPattern(const QString& pattern, const QString& key) const
{
    // 前缀匹配
    if (pattern.endsWith('/')) {
        return key.startsWith(pattern);
    }

    // 分段通配匹配
    if (pattern.contains('*')) {
        QStringList patParts = pattern.split('/');
        QStringList keyParts = key.split('/');
        return matchSegments(patParts, 0, keyParts, 0);
    }

    // 精确匹配
    return pattern == key;
}

bool SingletonAPI::matchPatternCached(const Subscription& sub, const QString& key) const
{
    if (sub.isPrefix) {
        return key.startsWith(sub.pattern);
    }

    if (!sub.isWildcard) {
        return sub.pattern == key;
    }

    // 使用缓存的 pattern 分段，避免重复 split
    if (sub.cachedPatternParts.isEmpty()) {
        sub.cachedPatternParts = sub.pattern.split('/');
    }

    QStringList keyParts = key.split('/');
    return matchSegments(sub.cachedPatternParts, 0, keyParts, 0);
}

QString SingletonAPI::extractWildcardPrefix(const QString& pattern)
{
    // 提取通配符之前的最后一段路径作为前缀索引 key
    // "sensor/**" → "sensor/"
    // "sensor/*/temp" → "sensor/"
    // "*/status" → "" (通配符在第一段，无法建立有效索引)
    int firstStar = pattern.indexOf('*');
    if (firstStar < 0) return QString();

    int lastSlash = pattern.lastIndexOf('/', firstStar);
    if (lastSlash < 0) return QString();

    return pattern.left(lastSlash + 1);
}

QList<SingletonAPI::Subscription> SingletonAPI::getMatchingSubscriptions(const QString& key) const
{
    QMutexLocker locker(&m_mutex);
    QList<Subscription> result;
    QSet<SubscriptionId> checked;

    // 1. 精确匹配
    auto exactIds = m_keyToSubIds.values(key);
    for (SubscriptionId id : exactIds) {
        auto it = m_subscriptions.find(id);
        if (it != m_subscriptions.end() && !it->isExpired()) {
            result.append(*it);
        }
    }

    // 2. 通配符匹配 — 优先通过前缀索引缩小范围
    int firstSlash = key.indexOf('/');
    if (firstSlash > 0) {
        QString keyPrefix = key.left(firstSlash + 1);
        auto indexIt = m_prefixIndex.find(keyPrefix);
        if (indexIt != m_prefixIndex.end()) {
            for (SubscriptionId id : indexIt.value()) {
                checked.insert(id);
                auto subIt = m_subscriptions.find(id);
                if (subIt != m_subscriptions.end() && !subIt->isExpired() && matchPatternCached(*subIt, key)) {
                    result.append(*subIt);
                }
            }
        }
    }

    // 3. 未索引的通配符（无有效前缀的 pattern，如 "*/value"、"**"）
    for (SubscriptionId id : m_wildcardSubs) {
        if (checked.contains(id)) continue;
        auto subIt = m_subscriptions.find(id);
        if (subIt != m_subscriptions.end() && !subIt->isExpired() && matchPatternCached(*subIt, key)) {
            result.append(*subIt);
        }
    }

    return result;
}

void SingletonAPI::notifySubscribers(const QString& key, const QVariant& value)
{
    // 取得匹配订阅的快照（释放锁后再执行回调，避免死锁）
    QList<Subscription> subs = getMatchingSubscriptions(key);

    for (const auto& sub : subs) {
        if (!sub.callback) continue;

        if (sub.context) {
            QObject* ctx = sub.context.data();
            if (!ctx) continue;  // 已销毁

            if (QThread::currentThread() == ctx->thread()) {
                // 同线程直接调用
                sub.callback(key, value);
            } else {
                // 跨线程：post 到 context 线程，QPointer guard 防止悬空
                QPointer<QObject> guard(ctx);
                QMetaObject::invokeMethod(ctx, [sub, key, value, guard]() {
                    if (!guard) return;
                    sub.callback(key, value);
                }, Qt::QueuedConnection);
            }
        } else {
            // 无 context：直接在当前线程调用
            sub.callback(key, value);
        }
    }
}

SubscriptionInfo SingletonAPI::toInfo(const Subscription& sub) const
{
    SubscriptionInfo info;
    info.id = sub.id;
    info.pattern = sub.pattern;
    info.isWildcard = sub.isWildcard;
    info.isPrefix = sub.isPrefix;

    if (!sub.context.isNull()) {
        info.contextName = sub.context->objectName();
        if (info.contextName.isEmpty())
            info.contextName = QString("%1").arg(reinterpret_cast<quintptr>(sub.context.data()), 0, 16);
        if (sub.context->thread())
            info.threadName = sub.context->thread()->objectName();
    }

    if (!sub.owner.isNull() && sub.owner != sub.context) {
        info.ownerName = sub.owner->objectName();
        if (info.ownerName.isEmpty())
            info.ownerName = QString("%1").arg(reinterpret_cast<quintptr>(sub.owner.data()), 0, 16);
    }

    return info;
}

// ========== 工具方法 ==========

QString SingletonAPI::formatVariant(const QVariant& v)
{
    if (!v.isValid()) return "(null)";

    int typeId = v.userType();

    // QStringList
    if (typeId == QMetaType::QStringList) {
        return v.toStringList().join(", ");
    }

    // QVariantList（递归）
    if (typeId == QMetaType::QVariantList) {
        QVariantList list = v.toList();
        QStringList parts;
        parts.reserve(list.size());
        for (const auto& item : list) {
            parts.append(formatVariant(item));
        }
        return "[" + parts.join(", ") + "]";
    }

    // QVariantMap（递归）
    if (typeId == QMetaType::QVariantMap) {
        QVariantMap map = v.toMap();
        QStringList parts;
        parts.reserve(map.size());
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            parts.append(QString("%1=%2").arg(it.key(), formatVariant(it.value())));
        }
        return "{" + parts.join(", ") + "}";
    }

    // QVariantHash（递归）
    if (typeId == QMetaType::QVariantHash) {
        QVariantHash hash = v.toHash();
        QStringList parts;
        parts.reserve(hash.size());
        for (auto it = hash.constBegin(); it != hash.constEnd(); ++it) {
            parts.append(QString("%1=%2").arg(it.key(), formatVariant(it.value())));
        }
        return "{" + parts.join(", ") + "}";
    }

    // QByteArray
    if (typeId == QMetaType::QByteArray) {
        QByteArray ba = v.toByteArray();
        if (ba.size() == 0) return "hex[0]: empty";
        if (ba.size() <= 32) {
            return QString("hex[%1]: %2").arg(ba.size())
                .arg(QString(ba.toHex(' ').toUpper()));
        }
        return QString("hex[%1]: %2...").arg(ba.size())
            .arg(QString(ba.left(16).toHex(' ').toUpper()));
    }

    // QJsonObject
    if (typeId == qMetaTypeId<QJsonObject>()) {
        QJsonDocument doc(v.toJsonObject());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }

    // QJsonArray
    if (typeId == qMetaTypeId<QJsonArray>()) {
        QJsonDocument doc(v.toJsonArray());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }

    // 自定义类型：尝试 QDebug 输出
    if (v.userType() >= QMetaType::User) {
        QString result;
        QDebug dbg(&result);
        dbg.nospace() << v;
        return result;
    }

    // 基本类型兜底
    return v.toString();
}
