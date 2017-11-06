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

    QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry, int rotation = 0);
    QPixmap MakeRoundedPixmap(QSize sz, QPixmap pm, qreal rx, qreal ry, qint64 time);

    uint32_t InhibitStandby();
    void UnInhibitStandby(uint32_t cookie);

    void MoveToCenter(QWidget* w);

    QString Time2str(qint64 seconds);

    bool ValidateScreenshotPath(const QString& path);
}
}

#endif /* ifndef _DMR_UTILS_H */
