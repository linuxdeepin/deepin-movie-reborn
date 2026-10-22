// SPDX-FileCopyrightText: 2024-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sysutils.h"
#include "ddlog.h"
#include <QDebug>
#include <QDir>
#include <QLibraryInfo>
#include <QLibrary>

SysUtils::SysUtils()
{
    qCDebug(appMovie) << "SysUtils instance created";
}

bool SysUtils::libExist(const QString &strlib)
{
    qCDebug(appMovie) << "Checking library existence:" << strlib;
    
    // find all library paths by QLibrary
    QString libName;
    if (strlib.contains(".so"))
        libName = strlib.mid(0, strlib.indexOf(".so"));
    else
        libName = strlib;
    
    qCDebug(appMovie) << "Attempting to load library:" << libName;
    QLibrary lib(libName);
    bool bExist = lib.load();
    if (!bExist) {
        qCWarning(appMovie) << "Failed to load library:" << lib.errorString();
        QString fullPath = libPath(strlib);
        qCDebug(appMovie) << "Trying alternative path:" << fullPath;
        lib.setFileName(fullPath);
        bExist = lib.load();
        if (bExist) {
            qCInfo(appMovie) << "Successfully loaded library from alternative path:" << fullPath;
        } else {
            qCWarning(appMovie) << "Failed to load library from alternative path:" << fullPath << "-" << lib.errorString();
        }
    } else {
        qCInfo(appMovie) << "Successfully loaded library:" << libName;
    }
    return bExist;
}

QString SysUtils::libPath(const QString &strlib)
{
    qCDebug(appMovie) << "Getting library path for:" << strlib;
    
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QString path = QLibraryInfo::path(QLibraryInfo::LibrariesPath);
#else   
    QString path = QLibraryInfo::path(QLibraryInfo::LibrariesPath);
#endif

    if (path.isEmpty()) {
        qCWarning(appMovie) << "Failed to get library path from QLibraryInfo";
        return strlib;
    }

    qCDebug(appMovie) << "Library search path:" << path;
    
    // 直接使用完整路径创建 QDir
    QDir dir(QDir::cleanPath(path));
    if (!dir.exists() || !dir.isReadable()) {
        qCWarning(appMovie) << "Directory not accessible:" << path;
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
                qCInfo(appMovie) << "Using fallback path:" << fbPath;
                break;
            }
        }
    }

    QStringList list = dir.entryList(QStringList{strlib + "*"}, 
                                   QDir::NoDotAndDotDot | QDir::Files);
    
    if (!list.isEmpty()) {
        if (list.contains(strlib)) {
            qCDebug(appMovie) << "Found exact library match:" << strlib;
            return strlib;
        }
        list.sort();
        QString result = list.last();
        qCDebug(appMovie) << "Found library variant:" << result;
        return result;
    }

    qCWarning(appMovie) << "No matching library found for:" << strlib;
    // Qt LibrariesPath 不包含，返回默认名称
    return strlib;
}
