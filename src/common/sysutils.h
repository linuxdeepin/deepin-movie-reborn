// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYSUTILS_H
#define SYSUTILS_H

#include <QString>

/**
 * @file 系统环境检测
 */
class SysUtils
{
public:
    SysUtils();
    /**
     * @brief 检查系统是否存在动态库
     * @param 动态库名称
     * @return 库是否存在
     */
    static bool libExist(const QString &strlib);

    /**
     * @brief 查找动态库真实名
     * @param 动态库so名
     * @return 库名称
     */
    static QString libPath(const QString &strlib);
};

#endif // SYSUTILS_H
