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
        setStyleSheet(
                "dmr--ThumbnailFrame {"
                "border-radius: 4px;"
                "border: 1px solid rgba(255, 255, 255, 0.1); }");
        auto e = new QGraphicsDropShadowEffect(this);
        //box-shadow: 0 2px 4px 0 rgba(0, 0, 0, 0.2);
        e->setColor(qRgba(0, 0, 0, 50));
        e->setOffset(0, 2);
        e->setBlurRadius(4);
        setGraphicsEffect(e);
    }
};


class BurstScreenshotsDialog: public DDialog {
    Q_OBJECT
public:
    BurstScreenshotsDialog(const PlayItemInfo& pif);
    void updateWithFrames(const QList<QImage>& frames);

public slots:
    int exec() override;

private:
    QGridLayout *_grid {nullptr};
    DTextButton *_saveBtn {nullptr};
};
}

#endif /* ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H */
