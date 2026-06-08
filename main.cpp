// MIT License
// Copyright (c) 2025 Chujh (QQ: 1206569273)
// See the LICENSE file in the project root for full license text.

/**
 * @file main.cpp
 * @brief SingletonAPI v4.0 多功能测试工具
 */
#include "SingletonAPI.h"
#include <QApplication>
#include <qDebug>
#include "SingleTestUi.h"
#include "SingletonAPIManager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qDebug() << "SingletonAPI v5.0 Test Tool";
    qDebug() << "Qt Version:" << QT_VERSION_STR;

    SingleTestUi w;
    w.show();
    SingletonAPIManager mgr;
    mgr.show();

    int ret = app.exec();

    // 在 QApplication 销毁前清理单例，避免静态析构顺序问题
    SingletonAPI::shutdown();

    return ret;
}
