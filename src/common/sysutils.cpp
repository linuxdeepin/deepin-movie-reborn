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
    return lib.load();
}

QString SysUtils::libPath(const QString &strlib)
{
    QDir  dir;
    QString path  = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    dir.setPath(path);
    QStringList list = dir.entryList(QStringList() << (strlib + "*"), QDir::NoDotAndDotDot | QDir::Files); //filter name with strlib
    if (list.contains(strlib)) {
        return strlib;
    } else {
        list.sort();
    }

    if(list.size() > 0)
        return list.last();

    // Qt LibrariesPath 不包含，返回默认名称
    return strlib;
}
