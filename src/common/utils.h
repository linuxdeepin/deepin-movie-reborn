#ifndef _DMR_UTILS_H
#define _DMR_UTILS_H 

#include <QtCore>

namespace dmr {
namespace utils {
    void ShowInFileManager(const QString &path);
    bool IsNamesSimilar(const QString& s1, const QString& s2);
    QFileInfoList FindSimilarFiles(const QFileInfo& fi);
    QString FastFileHash(const QFileInfo& fi);
    QString FullFileHash(const QFileInfo& fi);
    QFileInfoList& SortSimilarFiles(QFileInfoList& fil);
    QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry);
}
}

#endif /* ifndef _DMR_UTILS_H */
