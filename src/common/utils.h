#ifndef _DMR_UTILS_H
#define _DMR_UTILS_H 

#include <QtCore>

namespace dmr {
namespace utils {
    void ShowInFileManager(const QString &path);
    bool CompareNames(const QString& fileName1, const QString& fileName2);
    bool IsNamesSimilar(const QString& s1, const QString& s2);
    QFileInfoList FindSimilarFiles(const QFileInfo& fi);
    QString FastFileHash(const QFileInfo& fi);
    QString FullFileHash(const QFileInfo& fi);

    QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry);
    QPixmap MakeRoundedPixmap(QSize sz, QPixmap pm, qreal rx, qreal ry, qint64 time);

    int InhibitStandby();
    void UnInhibitStandby(int cookie);

    void MoveToCenter(QWidget* w);
}
}

#endif /* ifndef _DMR_UTILS_H */
