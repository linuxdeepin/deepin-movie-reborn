// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _DMR_DBUS_ADAPTOR
#define _DMR_DBUS_ADAPTOR

#include <QtDBus>

#include "mainwindow.h"

using namespace dmr;
/**
 * @file d-bus适配器，开放影院d-bus接口
 */
class ApplicationAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.movie")
    Q_CLASSINFO("D-Bus Introspection", ""
                "  <interface name=\"com.deepin.movie\">\n"

                "    <method name=\"openFiles\">\n"
                "      <arg direction=\"in\" type=\"none\" name=\"openFiles\"/>\n"
                "    </method>\n"

                "    <method name=\"openFile\">\n"
                "      <arg direction=\"in\" type=\"s\" name=\"openFile\"/>\n"
                "      <arg direction=\"out\" type=\"b\"/>\n"
                "    </method>\n"

                "  </interface>\n")

public:
    explicit ApplicationAdaptor(MainWindow *pMainWid);
    /**
     * @brief 读取d-bus接口参数值
     * @param dbus服务名
     * @param dbus路径
     * @param dbus接口名
     * @param 获取的参数名
     * @return 获取参数的值
     */
    static QVariant redDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface = QString(), const char *pPropert = "");
    /**
     * @brief 调用d-bus方法
     * @param d-bus服务名
     * @param d-bus路径
     * @param d-bus接口名
     * @param d-bus的方法
     * @return 方法的返回值
     */
    static QVariant redDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod);

public Q_SLOTS:
    /**
     * @brief 通过d-bus服务播放视频
     * @param 视频路径
     */
    bool openFile(const QString &sFile);
    /**
     * @brief 通过d-bus服务播放视频
     * @param 视频路径集合
     */
    void openFiles(const QStringList &listFiles);
    /**
     * @brief 调用mainwindow的raise方法
     */
    void Raise();

private:
    void initMember();

private:
    QTime m_oldTime;              ///记录上次播放时的时间
    MainWindow *m_pMainWindow;
};


#endif /* ifndef _DMR_DBUS_ADAPTOR */
