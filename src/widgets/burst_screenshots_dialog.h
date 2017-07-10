#ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H
#define _DMR_BURST_SCREENSHOTS_DIALOG_H 

#include <QtWidgets>
#include <ddialog.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
class PlayerEngine;

class BurstScreenshotsDialog: public DDialog {
public:
    BurstScreenshotsDialog(PlayerEngine*);

public slots:
    int exec() override;
    void OnScreenshot(const QPixmap& frame);

private:
    PlayerEngine *_engine {nullptr};
    int _count {0};
    QGridLayout *_grid {nullptr};
};
}

#endif /* ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H */
