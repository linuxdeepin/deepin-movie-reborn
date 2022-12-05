#include "libraryloader.h"
#include <QLibraryInfo>
#include <QLibrary>
#include <QDir>
#include <QDebug>

QStringList LibraryLoader::findAllLib(const QString &sLib)
{
    QStringList paths;
    paths = QString::fromLatin1(qgetenv("LD_LIBRARY_PATH"))
            .split(QLatin1Char(':'), QString::SkipEmptyParts);
    paths << QLatin1String("/usr/lib") << QLatin1String("/usr/local/lib");
    const QString qtLibPath = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    if (qtLibPath != "/usr/lib" && qtLibPath != "/usr/local/lib")
        paths << qtLibPath;

    QStringList foundLibs;
    foreach (const QString &path, paths) {
        QDir dir = QDir(path);
        QStringList entryList = dir.entryList(QStringList() << QString("%1.*").arg(sLib), QDir::NoDotAndDotDot | QDir::Files);
        qSort(entryList.begin(), entryList.end(), libGreaterThan);
        foreach (const QString &entry, entryList)
            foundLibs << path + QLatin1Char('/') + entry;
    }
    return foundLibs;
}

QString LibraryLoader::libPath(const QString &sLib)
{
    QStringList paths;
    paths = QString::fromLatin1(qgetenv("LD_LIBRARY_PATH"))
            .split(QLatin1Char(':'), QString::SkipEmptyParts);
    paths << QLatin1String("/usr/lib") << QLatin1String("/usr/local/lib");
    const QString qtLibPath = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    if (qtLibPath != "/usr/lib" && qtLibPath != "/usr/local/lib")
        paths << qtLibPath;
    foreach (const QString &path, paths) {
        QDir dir = QDir(path);
        QStringList entryList = dir.entryList(QStringList() << QString("%1.*").arg(sLib), QDir::NoDotAndDotDot | QDir::Files);
        if (entryList.contains(sLib)) {
           qDebug() << "libPath: " << path << " " << sLib;
           return path + "/" + sLib;
        }
        if (!entryList.empty()) {
           qSort(entryList.begin(), entryList.end(), libGreaterThan);
           qDebug() << "libPath: " << path << " " << entryList.first();
           return path + "/" + entryList.first();
        }
    }
    qDebug() << "libPath: not find:" << sLib;
    return QString();
}

bool LibraryLoader::isLibExists(const QString &sLib)
{
    // qDebug() << "isLibExists: " << sLib << " " << (libPath(sLib).size() > 0);
    return libPath(sLib).size() > 0;
}

QString LibraryLoader::libGpuInfoPath()
{
    QStringList paths = findAllLib("libgpuinfo.so");
    QLibrary library;
    foreach (const QString &path, paths) {
      library.setFileName(path);
      if (library.resolve("vdp_Iter_decoderInfo")) {
        qDebug() << "libGpuInfoPath: find " << path;
        return path;
      } else {
        qDebug() << "libGpuInfoPath: " << path << "Cannot resolve the symbol or load GpuInfo library";
        qDebug() << library.errorString();
      }
    }
    qDebug() << "libGpuInfoPath: not find";
    return QString();
}

bool LibraryLoader::libGreaterThan(const QString &lhs, const QString &rhs)
{
    QStringList lhsparts = lhs.split(QLatin1Char('.'));
    QStringList rhsparts = rhs.split(QLatin1Char('.'));
    Q_ASSERT(lhsparts.count() > 1 && rhsparts.count() > 1);

    for (int i = 1; i < rhsparts.count(); ++i) {
        if (lhsparts.count() <= i)
            // left hand side is shorter, so it's less than rhs
            return false;

        bool ok = false;
        int b = 0;
        int a = lhsparts.at(i).toInt(&ok);
        if (ok)
            b = rhsparts.at(i).toInt(&ok);
        if (ok) {
            // both toInt succeeded
            if (a == b)
                continue;
            return a > b;
        } else {
            // compare as strings;
            if (lhsparts.at(i) == rhsparts.at(i))
                continue;
            return lhsparts.at(i) > rhsparts.at(i);
        }
    }

    // they compared strictly equally so far
    // lhs cannot be less than rhs
    return true;
}
