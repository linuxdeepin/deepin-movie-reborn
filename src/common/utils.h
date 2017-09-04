#ifndef _DMR_UTILS_H
#define _DMR_UTILS_H 

#include <QtCore>

namespace dmr {
namespace utils {
    void ShowInFileManager(const QString &path);
    QFileInfoList FindSimilarFiles(const QFileInfo& fi);
    QString FastFileHash(const QFileInfo& fi);
    QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry);
}
}

#endif /* ifndef _DMR_UTILS_H */
