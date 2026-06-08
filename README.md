SingletonAPI - Qt C++ 键值存储与事件总线系统

📋 目录

概述

✨ 特性

🚀 快速开始

📖 核心 API

🔧 使用示例

🧵 线程安全

🏗️ 集成指南

⚡ 性能优化

📊 使用场景

🤝 贡献指南

📄 许可证

概述

SingletonAPI 是一个为 Qt C++ 应用程序设计的轻量级、线程安全的全局状态管理库。它结合了键值存储和发布-订阅机制，让应用程序的状态管理和组件间通信变得更加简单、可靠。

主要用途：

应用程序全局状态管理

组件间松耦合通信

配置和用户设置存储

事件总线系统

跨线程数据同步

✨ 特性
🎯 核心功能

✅ 键值存储: 线程安全的全局数据存储

✅ 自动通知: 值变更时自动通知所有订阅者

✅ 类型安全: 编译时类型检查的泛型读取

✅ 通配符支持: 使用 *订阅多个相关键

✅ 前缀匹配: 按前缀批量订阅和管理

🚀 高级特性

✅ 线程安全: 内置细粒度锁，支持多线程并发访问

✅ 自动跨线程: Qt 信号槽自动处理线程边界

✅ 自动清理: 订阅者对象销毁时自动移除订阅

✅ 变更过滤: 避免相同值的重复通知

✅ 防抖功能: 防止短时间内频繁触发

✅ 变更日志: 可选的值变更历史记录

✅ 批量操作: 合并通知，提高性能

✅ 管理界面: 完整的订阅和状态监控

🚀 快速开始
1. 安装

将 SingletonAPI.h和 SingletonAPI.cpp添加到你的 Qt 项目中：

qmake
# 在你的 .pro 文件中添加
HEADERS += SingletonAPI.h
SOURCES += SingletonAPI.cpp
2. 基本使用
cpp
#include "SingletonAPI.h"
#include <QDebug>

// 获取单例实例
SingletonAPI& api = SingletonAPI::instance();

// 设置值
api.setValue("app/name", "MyApplication");
api.setValue("config/version", 1.0);

// 获取值
QString appName = api.getValue<QString>("app/name");
double version = api.getValue<double>("config/version");

// 订阅值变化
api.subscribe("config/*", [](const QString& key, const QVariant& value) {
    qDebug() << "配置更新:" << key << "=" << value;
});

// 当设置新值时自动触发回调
api.setValue("config/theme", "dark");  // 触发上面的订阅回调
📖 核心 API
🔧 值操作

方法



描述



示例




setValue(key, value, forceNotify)



设置值并通知订阅者



api.setValue("counter", 42)




getValue<T>(key, defaultValue)



类型安全读取



int x = api.getValue<int>("count")




setValues(map, forceNotify)



批量设置值



api.setValues({{"a",1},{"b",2}})




getValues(keys)



批量获取值



api.getValues({"a","b"})




remove(key, notify)



删除键值对



api.remove("temp", true)




clear(notify)



清空所有值



api.clear()

📡 订阅管理

方法



描述



示例




subscribe(key, callback, context)



订阅值更新



api.subscribe("data", callback, this)




subscribeByPrefix(prefix, callback, context)



前缀订阅



api.subscribeByPrefix("config/", callback)




subscribeOnce(key, callback, context)



一次性订阅



api.subscribeOnce("init", callback)




unsubscribe(id)



取消订阅



api.unsubscribe(subId)




unsubscribeByKey(key)



取消键的所有订阅



api.unsubscribeByKey("data")




unsubscribeByOwner(owner)



取消所有者的所有订阅



api.unsubscribeByOwner(this)

🔍 查询工具

方法



描述



示例




contains(key)



检查键是否存在



api.contains("user/name")




keys()



获取所有键



QStringList allKeys = api.keys()




keysByPrefix(prefix)



获取匹配前缀的键



api.keysByPrefix("sensor/")




removeByPrefix(prefix, notify)



批量删除键



api.removeByPrefix("temp/")

📊 监控诊断

方法



描述



示例




allSubscriptions()



获取所有订阅信息



api.allSubscriptions()




subscriptionCount()



获取订阅总数



int count = api.subscriptionCount()




valueCount()



获取值数量



int values = api.valueCount()




statistics()



获取使用统计



qDebug() << api.statistics()

🔧 使用示例
配置管理系统
cpp
// 设置配置
api.setValues({
    {"config/ui/theme", "dark"},
    {"config/ui/language", "zh_CN"},
    {"config/network/timeout", 30},
    {"config/storage/path", "/data"}
});

// 监听所有配置变更
api.subscribeByPrefix("config/", [](const QString& key, const QVariant& value) {
    qDebug() << "配置变更:" << key << "->" << value;
    
    // 保存到文件或数据库
    saveConfigToFile(key, value);
});
事件总线系统
cpp
// 发布事件
void onUserLogin(const User& user) {
    api.setValue("event/user/login", QVariant::fromValue(user));
}

void onFileSaved(const QString& path) {
    api.setValue("event/file/saved", path);
}

// 订阅事件
class ActivityLogger : public QObject {
    Q_OBJECT
public:
    ActivityLogger() {
        SingletonAPI& api = SingletonAPI::instance();
        api.subscribe("event/*", this, &ActivityLogger::onEvent);
    }
    
private slots:
    void onEvent(const QString& key, const QVariant& data) {
        QString timestamp = QDateTime::currentDateTime().toString();
        m_logFile.writeLine(timestamp + " - " + key + ": " + data.toString());
    }
};
跨线程通信
cpp
// 在 UI 线程
MainWindow::MainWindow(QWidget *parent) 
    : QMainWindow(parent) {
    
    // 监听后台任务进度
    SingletonAPI& api = SingletonAPI::instance();
    api.subscribe("task/progress", this, [this](const QString&, const QVariant& value) {
        // 在主线程执行，安全更新 UI
        m_progressBar->setValue(value.toInt());
    });
}

// 在工作线程
void WorkerThread::run() {
    for (int i = 0; i <= 100; i++) {
        // 线程安全地更新进度
        SingletonAPI::instance().setValue("task/progress", i);
        QThread::msleep(100);
    }
}
防抖控制
cpp
// UI 输入框，防止用户快速输入时频繁处理
void MyLineEdit::onTextChanged(const QString& text) {
    // 300ms 内只触发最后一次
    SingletonAPI::instance().setValueDebounced("filter/text", text, 300);
}

// 处理函数
void setupSearch() {
    SingletonAPI& api = SingletonAPI::instance();
    api.subscribe("filter/text", [](const QString&, const QVariant& value) {
        // 只会被调用一次，即使文本快速变化
        performSearch(value.toString());
    });
}
🧵 线程安全

SingletonAPI 设计为完全线程安全：

安全特性

内部锁保护：所有公共方法都通过 QMutex保护

自动线程调度：回调在订阅者所在线程执行

上下文安全：指定上下文对象避免回调在已销毁对象上执行

原子操作：ID 生成等使用原子操作

线程使用示例
cpp
// 线程 1: 生产者
void ProducerThread::run() {
    while (m_running) {
        Data data = generateData();
        SingletonAPI::instance().setValue("sensor/data", QVariant::fromValue(data));
        QThread::msleep(100);
    }
}

// 线程 2: 消费者
void ConsumerThread::run() {
    SingletonAPI& api = SingletonAPI::instance();
    
    // 安全的跨线程订阅
    api.subscribe("sensor/data", [this](const QString&, const QVariant& value) {
        Data data = value.value<Data>();
        processData(data);
    });
}
🏗️ 集成指南
1. 项目配置

在你的 .pro文件中：

makefile
# 如果作为共享库构建
DEFINES += SINGLETONAPI_LIBRARY
SOURCES += SingletonAPI.cpp
HEADERS += SingletonAPI.h

# 如果使用静态库
DEFINES += SINGLETONAPI_USE_LIB
2. 应用程序生命周期
cpp
#include "SingletonAPI.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 初始化
    SingletonAPI& api = SingletonAPI::instance();
    
    // 应用程序逻辑...
    MainWindow window;
    window.show();
    
    int result = app.exec();
    
    // 可选：显式清理
    SingletonAPI::shutdown();
    
    return result;
}
3. 内存管理最佳实践
cpp
class MyWidget : public QWidget {
    Q_OBJECT
public:
    explicit MyWidget(QWidget *parent = nullptr) 
        : QWidget(parent) {
        
        // 将 this 作为上下文，对象销毁时自动清理订阅
        SingletonAPI& api = SingletonAPI::instance();
        m_subscriptionId = api.subscribe("ui/update", 
            [this](const QString&, const QVariant& value) {
                updateUI(value);
            }, 
            this  // 重要：指定上下文对象
        );
    }
    
    ~MyWidget() {
        // 可选：手动取消订阅
        SingletonAPI::instance().unsubscribe(m_subscriptionId);
    }
    
private:
    SubscriptionId m_subscriptionId;
};
⚡ 性能优化
1. 批量操作
cpp
// ❌ 不推荐：多次触发通知
api.setValue("config/a", 1);
api.setValue("config/b", 2);
api.setValue("config/c", 3);

// ✅ 推荐：单次触发通知
QMap<QString, QVariant> updates = {
    {"config/a", 1},
    {"config/b", 2},
    {"config/c", 3}
};
api.setValues(updates);
2. 通配符使用建议
cpp
// ✅ 精确匹配（最快）
api.subscribe("specific/key", callback);

// ⚡ 前缀匹配（快速）
api.subscribeByPrefix("config/", callback);

// ⚠️ 通用通配符（较慢）
api.subscribe("*", callback);  // 谨慎使用
3. 内存管理
cpp
// 定期清理过期订阅
QTimer* cleanupTimer = new QTimer(this);
connect(cleanupTimer, &QTimer::timeout, []() {
    // 内部会自动清理
});
cleanupTimer->start(30000);  // 每30秒
📊 使用场景
场景 1: 应用程序状态管理
cpp
// 管理应用全局状态
class ApplicationState {
public:
    void initialize() {
        SingletonAPI& api = SingletonAPI::instance();
        
        // 初始状态
        api.setValues({
            {"app/state", "initializing"},
            {"app/user", QVariant()},
            {"app/connection", false}
        });
        
        // 状态监听
        api.subscribe("app/state", [](const QString&, const QVariant& value) {
            qDebug() << "应用状态变更:" << value.toString();
        });
    }
    
    void login(const User& user) {
        SingletonAPI& api = SingletonAPI::instance();
        api.setValues({
            {"app/state", "logged_in"},
            {"app/user", QVariant::fromValue(user)}
        });
    }
};
场景 2: 插件系统通信
cpp
// 插件管理器
class PluginManager {
public:
    void registerPlugin(const QString& pluginId, QObject* plugin) {
        SingletonAPI& api = SingletonAPI::instance();
        
        // 插件状态
        api.setValue("plugin/" + pluginId + "/status", "registered");
        
        // 插件间通信通道
        api.subscribeByPrefix("plugin/" + pluginId + "/event/", 
            [plugin](const QString& key, const QVariant& data) {
                QMetaObject::invokeMethod(plugin, "onPluginEvent",
                    Q_ARG(QString, key),
                    Q_ARG(QVariant, data));
            }, 
            plugin
        );
    }
};
场景 3: 实时数据监控
cpp
// 数据采集器
class DataCollector {
public:
    void start() {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &DataCollector::collectData);
        m_timer->start(1000);
    }
    
private slots:
    void collectData() {
        SingletonAPI& api = SingletonAPI::instance();
        
        // 采集传感器数据
        double temp = readTemperature();
        double humi = readHumidity();
        
        // 批量更新
        QMap<QString, QVariant> data = {
            {"sensor/temperature", temp},
            {"sensor/humidity", humi},
            {"sensor/timestamp", QDateTime::currentMSecsSinceEpoch()}
        };
        
        api.setValues(data);
        
        // 触发报警检查
        if (temp > 50.0) {
            api.setValue("alert/high_temperature", temp);
        }
    }
};
🤝 贡献指南
提交问题

在 Issues 页面描述问题

提供复现步骤

包含系统环境和 Qt 版本信息

提交代码

Fork 本仓库

创建功能分支 (git checkout -b feature/amazing-feature)

提交更改 (git commit -m 'Add some amazing feature')

推送到分支 (git push origin feature/amazing-feature)

创建 Pull Request

代码规范

遵循 Qt 编码规范

添加适当的注释

为新功能添加单元测试

更新 README 和文档

📄 许可证

本项目采用 MIT 许可证。详见 LICENSE
文件。

📞 获取帮助

📖 文档: 查看本 README 和头文件注释

🐛 问题: 提交到 Issues

💡 示例: 查看 examples/目录

🎯 特性请求: 在 Issues 中描述你的需求

Star 这个项目 ⭐ 如果你觉得它有用！

欢迎贡献代码、报告问题或提出改进建议！