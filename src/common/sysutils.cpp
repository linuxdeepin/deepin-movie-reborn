// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sysutils.h"
#include <QDebug>
#include <QDir>
#include <QLibraryInfo>
#include <QLibrary>

SysUtils::SysUtils()
{

}

bool SysUtils::libExist(const QString &strlib)
{
    // find all library paths by QLibrary
    QString libName;
    if (strlib.contains(".so"))
        libName = strlib.mid(0, strlib.indexOf(".so"));
    else
        libName = strlib;
    QLibrary lib(libName);
    bool bExist = lib.load();
    if (!bExist) {
        qWarning() << "Failed to load library:" << lib.errorString();
        lib.setFileName(libPath(strlib));
        bExist = lib.load();
    }
    return bExist;
}

QString SysUtils::libPath(const QString &strlib)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QString path = QLibraryInfo::path(QLibraryInfo::LibrariesPath);
#else   
    QString path = QLibraryInfo::path();
#endif

    if (path.isEmpty()) {
        qWarning() << "Failed to get library path";
        return strlib;
    }

    // 直接使用完整路径创建 QDir
    QDir dir(QDir::cleanPath(path));
    if (!dir.exists() || !dir.isReadable()) {
        qWarning() << "目录无法访问:" << path;
        // 尝试使用备选路径
        QStringList fallbackPaths = {
            "/usr/lib",
            "/usr/local/lib",
            QDir::homePath() + "/.local/lib"
        };
        
        for (const QString &fbPath : std::as_const(fallbackPaths)) {
            QDir fallbackDir(fbPath);
            if (fallbackDir.exists() && fallbackDir.isReadable()) {
                dir = fallbackDir;
                qWarning() << "使用备选路径:" << fbPath;
                break;
            }
        }
    }

    QStringList list = dir.entryList(QStringList{strlib + "*"}, 
                                   QDir::NoDotAndDotDot | QDir::Files);
    
    if (!list.isEmpty()) {
        if (list.contains(strlib)) {
            return strlib;
        }
        list.sort();
        return list.last();
    }

    // Qt LibrariesPath 不包含，返回默认名称
    return strlib;
}
