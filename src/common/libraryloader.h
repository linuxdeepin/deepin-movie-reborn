#ifndef LIBRARYLOADER_H
#define LIBRARYLOADER_H

#include <QString>

class LibraryLoader
{
public:
    LibraryLoader() = default;
    static QStringList findAllLib(const QString &sLib);
    static QString libPath(const QString &sLib);
    static bool isLibExists(const QString &sLib);
    static QString libGpuInfoPath();

private:
    static bool libGreaterThan(const QString &lhs, const QString &rhs);
};

#endif // LIBRARYLOADER_H
