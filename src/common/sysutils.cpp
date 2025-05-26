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
    qDebug() << "SysUtils instance created";
}

bool SysUtils::libExist(const QString &strlib)
{
    qDebug() << "Checking library existence:" << strlib;
    
    // find all library paths by QLibrary
    QString libName;
    if (strlib.contains(".so"))
        libName = strlib.mid(0, strlib.indexOf(".so"));
    else
        libName = strlib;
    
    qDebug() << "Attempting to load library:" << libName;
    QLibrary lib(libName);
    bool bExist = lib.load();
    if (!bExist) {
        qWarning() << "Failed to load library:" << lib.errorString();
        QString fullPath = libPath(strlib);
        qDebug() << "Trying alternative path:" << fullPath;
        lib.setFileName(fullPath);
        bExist = lib.load();
        if (bExist) {
            qInfo() << "Successfully loaded library from alternative path:" << fullPath;
        } else {
            qWarning() << "Failed to load library from alternative path:" << fullPath << "-" << lib.errorString();
        }
    } else {
        qInfo() << "Successfully loaded library:" << libName;
    }
    return bExist;
}

QString SysUtils::libPath(const QString &strlib)
{
    qDebug() << "Getting library path for:" << strlib;
    
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QString path = QLibraryInfo::path(QLibraryInfo::LibrariesPath);
#else   
    QString path = QLibraryInfo::path();
#endif

    if (path.isEmpty()) {
        qWarning() << "Failed to get library path from QLibraryInfo";
        return strlib;
    }

    qDebug() << "Library search path:" << path;
    
    // 直接使用完整路径创建 QDir
    QDir dir(QDir::cleanPath(path));
    if (!dir.exists() || !dir.isReadable()) {
        qWarning() << "Directory not accessible:" << path;
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
                qInfo() << "Using fallback path:" << fbPath;
                break;
            }
        }
    }

    QStringList list = dir.entryList(QStringList{strlib + "*"}, 
                                   QDir::NoDotAndDotDot | QDir::Files);
    
    if (!list.isEmpty()) {
        if (list.contains(strlib)) {
            qDebug() << "Found exact library match:" << strlib;
            return strlib;
        }
        list.sort();
        QString result = list.last();
        qDebug() << "Found library variant:" << result;
        return result;
    }

    qWarning() << "No matching library found for:" << strlib;
    // Qt LibrariesPath 不包含，返回默认名称
    return strlib;
}
