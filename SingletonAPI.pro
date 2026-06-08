QT += widgets core

CONFIG += c++17

# ============================================================
#  编译模式开关
#    BUILD_AS_LIB = 1  → 编译为 SingletonAPI 共享库 (.dll/.so)
#    BUILD_AS_LIB = 0  → 编译为测试程序 (默认)
# ============================================================
BUILD_AS_LIB = 0

equals(BUILD_AS_LIB, 1) {
    # ========== 库模式 ==========
    message(">>> Building as SHARED LIBRARY: SingletonAPI")
    TEMPLATE = lib
    DEFINES += SINGLETONAPI_LIBRARY
    TARGET = SingletonAPI

    HEADERS += SingletonAPI.h SingletonAPIManager.h
    SOURCES += SingletonAPI.cpp SingletonAPIManager.cpp
    FORMS   += SingletonAPIManager.ui
} else {
    # ========== 应用模式（默认）==========
    message(">>> Building as TEST APPLICATION")

    TEMPLATE = app
    TARGET = SingletonAPI_Test

    SOURCES += \
        SingleTestUi.cpp \
        SingletonAPIManager.cpp \
        SingletonAPI.cpp \
        main.cpp

    HEADERS += \
        SingleTestUi.h \
        SingletonAPIManager.h \
        SingletonAPI.h

    FORMS += \
        SingleTestUi.ui \
        SingletonAPIManager.ui
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
