# SingletonAPI
QPropertyBus：响应式键值总线  setValue(key,value)存储并自动通知订阅者。subscribe(key,cb)支持Lambda/Qt槽及通配符*匹配。getValue&lt;T>(key)类型安全读取。valueChanged信号自动跨线程。  特性：通配符匹配、批量更新、变更过滤、自动跨线程回调、QObject订阅者自动清理。
