SingletonAPI
一个为 Qt C++ 应用程序设计的轻量级、线程安全的键值存储与事件总线系统。它让应用程序的全局状态管理和组件间通信变得简单可靠。

✨ 特性一览
🎯 核心能力

🔐 线程安全​ - 内置锁保护，多线程无忧

📡 发布/订阅​ - 自动通知所有订阅者

🔧 类型安全​ - 编译时类型检查

🎯 通配符匹配​ - 支持 *模式订阅

📁 前缀批量操作​ - 按前缀管理相关键

🚀 高级功能

🔄 自动跨线程​ - 回调自动在正确线程执行

🧹 自动清理​ - 订阅者销毁时自动移除

⏱️ 防抖控制​ - 防止频繁触发

📊 变更日志​ - 完整的值变更历史

⚡ 批量优化​ - 合并通知提升性能

👀 监控诊断​ - 全面的状态查询接口

🚀 5分钟上手
1. 添加到项目
qmake
# 在你的 .pro 文件中
HEADERS += SingletonAPI.h
SOURCES += SingletonAPI.cpp
2. 基本使用
cpp
#include "SingletonAPI.h"

// 获取实例
SingletonAPI& api = SingletonAPI::instance();

// 📝 设置值
api.setValue("config/theme", "dark");
api.setValue("counter", 42);

// 📖 获取值
QString theme = api.getValue<QString>("config/theme");
int count = api.getValue<int>("counter");

// 👂 监听变化
api.subscribe("config/*", [](const QString& key, const QVariant& value) {
    qDebug() << key << "更新为:" << value;
});

// 🔥 触发回调
api.setValue("config/theme", "light");  // 立即触发上面的监听
📖 核心 API 速查
值操作

方法



描述



示例




setValue(key, value)



设置单个值



api.setValue("count", 1)




getValue<T>(key)



类型安全读取



int x = api.getValue<int>("count")




setValues({...})



批量设置



api.setValues({{"a",1},{"b",2}})




remove(key)



删除键



api.remove("temp")

订阅管理

方法



描述



示例




subscribe(key, callback)



订阅变化



api.subscribe("data", callback)




subscribeByPrefix(pre, cb)



前缀订阅



api.subscribeByPrefix("user/", cb)




subscribeOnce(key, cb)



一次性订阅



api.subscribeOnce("init", cb)




unsubscribe(id)



取消订阅



api.unsubscribe(id)

查询工具

方法



描述




contains(key)



检查键是否存在




keys()



获取所有键




keysByPrefix(pre)



获取匹配前缀的键

🔧 实用示例
配置管理
cpp
// 批量设置配置
api.setValues({
    {"config/ui/theme", "dark"},
    {"config/ui/language", "zh_CN"},
    {"config/network/timeout", 30}
});

// 监听所有配置变更
api.subscribeByPrefix("config/", [](const QString& key, const QVariant& value) {
    qDebug() << "配置变更:" << key << "→" << value;
    saveConfigToFile(key, value);  // 自动持久化
});
事件总线
cpp
// 🔔 发布事件
void onUserLogin(User user) {
    api.setValue("event/user/login", QVariant::fromValue(user));
}

// 👂 监听所有事件
class Logger : public QObject {
public:
    Logger() {
        api.subscribe("event/*", this, [this](const QString& event, const QVariant& data) {
            log("事件:", event, "数据:", data);
        });
    }
};
跨线程安全更新
cpp
// 🖥️ UI 线程
MainWindow::MainWindow() {
    // 安全监听后台进度
    api.subscribe("task/progress", this, [this](auto, const QVariant& value) {
        m_progressBar->setValue(value.toInt());  // ✅ 在主线程执行
    });
}

// 🔧 工作线程
void WorkerThread::run() {
    for (int i = 0; i <= 100; i++) {
        api.setValue("task/progress", i);  // ✅ 线程安全
        QThread::msleep(100);
    }
}
防抖搜索
cpp
// 用户输入时防抖
void MyLineEdit::onTextChanged(const QString& text) {
    // 300ms 内只触发最后一次
    api.setValueDebounced("search/text", text, 300);
}

// 处理搜索
void setupSearch() {
    api.subscribe("search/text", [](const QString&, const QVariant& value) {
        performSearch(value.toString());  // 只会触发一次
    });
}
🧵 线程安全设计
🔐 安全特性

特性



说明



优势




内部锁保护​



所有方法线程安全



无需额外同步




自动线程调度​



回调在订阅者线程执行



避免跨线程问题




上下文感知​



指定上下文对象自动清理



防止悬空回调




原子操作​



ID 生成等使用原子操作



高性能无锁操作

生产者-消费者模式
cpp
// 🎬 生产者线程
void Producer::run() {
    while (running) {
        Data data = fetchData();
        api.setValue("sensor/1", data);  // ✅ 线程安全设置
    }
}

// 🍽️ 消费者线程
void Consumer::run() {
    api.subscribe("sensor/1", [this](auto, const QVariant& value) {
        Data data = value.value<Data>();
        process(data);  // ✅ 在消费者线程处理
    });
}
🏗️ 集成指南
1. 基础集成
cpp
#include "SingletonAPI.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 获取实例
    SingletonAPI& api = SingletonAPI::instance();
    
    // 你的应用逻辑...
    
    return app.exec();
}
2. 内存管理最佳实践
cpp
class MyWidget : public QWidget {
public:
    MyWidget() {
        // ✅ 推荐：使用 context 自动清理
        m_subId = api.subscribe("data/update", 
            [this](auto, auto) { updateUI(); }, 
            this  // 关键：this 作为上下文
        );
    }
    
    ~MyWidget() {
        // 可选手动清理
        api.unsubscribe(m_subId);
    }
    
private:
    SubscriptionId m_subId;
};
⚡ 性能优化建议
1. 批量操作优化
cpp
// ❌ 低效：触发多次通知
api.setValue("config/a", 1);
api.setValue("config/b", 2);
api.setValue("config/c", 3);

// ✅ 高效：只触发一次通知
QMap<QString, QVariant> updates = {
    {"config/a", 1},
    {"config/b", 2},
    {"config/c", 3}
};
api.setValues(updates);
2. 订阅模式选择
cpp
// 性能从高到低：
api.subscribe("exact/key", callback);          // ✅ 精确匹配（最快）
api.subscribeByPrefix("config/", callback);    // ⚡ 前缀匹配（快）
api.subscribe("sensor/*", callback);           // 🔄 通配符（中等）
api.subscribe("*", callback);                 // ⚠️ 全匹配（谨慎使用）
3. 定期清理
cpp
// 可选：定期清理过期订阅
QTimer::singleShot(30000, []() {
    // 内部自动清理
});
📊 典型应用场景
场景 1：全局状态管理
cpp
class AppState {
public:
    void login(const User& user) {
        api.setValues({
            {"app/state", "logged_in"},
            {"app/user", QVariant::fromValue(user)},
            {"app/login_time", QDateTime::currentDateTime()}
        });
    }
    
    void logout() {
        api.setValues({
            {"app/state", "logged_out"},
            {"app/user", QVariant()}
        });
    }
};
场景 2：插件通信
cpp
class PluginSystem {
public:
    void sendEvent(const QString& plugin, const QString& event, const QVariant& data) {
        QString key = QString("plugin/%1/event/%2").arg(plugin).arg(event);
        api.setValue(key, data);
    }
    
    void listenPlugin(const QString& plugin, QObject* receiver) {
        api.subscribeByPrefix("plugin/" + plugin + "/event/", 
            receiver, SLOT(onPluginEvent(QString, QVariant)));
    }
};
场景 3：实时监控
cpp
class SensorMonitor {
public:
    void start() {
        m_timer.start(1000, this, [this]() {
            api.setValues({
                {"sensor/temp", readTemperature()},
                {"sensor/humi", readHumidity()},
                {"sensor/time", QTime::currentTime()}
            });
        });
    }
};
🎯 高级功能
变更历史记录
cpp
// 启用变更日志
api.enableChangeLog(1000);  // 每个键保存1000条记录

// 查看历史
auto history = api.changeHistory("config/theme");
for (auto& record : history) {
    qDebug() << record.timestamp 
             << record.key 
             << ":" << record.oldValue 
             << "→" << record.newValue;
}
管理界面
cpp
// 获取所有订阅信息
auto subs = api.allSubscriptions();
for (auto& info : subs) {
    qDebug() << "ID:" << info.id
             << "Pattern:" << info.pattern
             << "Context:" << info.contextName;
}

// 获取统计信息
qDebug() << "统计:" << api.statistics();
// 输出示例: "值: 42, 订阅: 15, 通配符: 3"
🤝 贡献指南
报告问题

在 Issues
中描述问题

提供最小可复现代码

注明系统环境和 Qt 版本

提交代码
bash
# 1. Fork 仓库
# 2. 创建功能分支
git checkout -b feature/amazing-feature

# 3. 提交更改
git commit -m '添加了很棒的功能'

# 4. 推送到分支
git push origin feature/amazing-feature

# 5. 创建 Pull Request
开发要求

遵循 Qt 编码规范

为新功能添加单元测试

更新相关文档

保持向后兼容性

📄 许可证

MIT License - 详见 LICENSE
文件。

📞 支持与反馈

📖 详细文档: 查看头文件注释

🐛 问题反馈: GitHub Issues

💡 功能建议: 在 Issues 中描述

🌟 Star: 如果这个项目对你有帮助

💖 感谢使用 SingletonAPI！

如果你觉得这个库有用，请给它一个 ⭐ 支持我们的工作！