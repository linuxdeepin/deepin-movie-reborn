#ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H
#define _DMR_BURST_SCREENSHOTS_DIALOG_H 

#include <QtWidgets>
#include <ddialog.h>
#include <dtextbutton.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
class PlayerEngine;
class PlayItemInfo;

class ThumbnailFrame: public QLabel { 
    Q_OBJECT
public:
    ThumbnailFrame(QWidget* parent) :QLabel(parent) {
        setFixedSize(178, 100);
        auto e = new QGraphicsDropShadowEffect(this);
        e->setColor(QColor(0, 0, 0, 255 * 2 / 10));
        e->setOffset(0, 2);
        e->setBlurRadius(4);
        setGraphicsEffect(e);
    }
};


class BurstScreenshotsDialog: public DDialog {
    Q_OBJECT
public:
    BurstScreenshotsDialog(const PlayItemInfo& pif);
    void updateWithFrames(const QList<QPair<QImage, qint64>>& frames);

    QString savedPosterPath();

public slots:
    int exec() override;
    void saveShootings();
    void savePoster();

private:
    QGridLayout *_grid {nullptr};
    DTextButton *_saveBtn {nullptr};
    QList<QPair<QImage, qint64>> _thumbs;
    QString _posterPath;
};
}

#endif /* ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H */
