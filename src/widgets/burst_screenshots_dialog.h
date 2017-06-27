#ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H
#define _DMR_BURST_SCREENSHOTS_DIALOG_H 

#include <QtWidgets>
#include <ddialog.h>
#include "mpv_proxy.h"

DWIDGET_USE_NAMESPACE

namespace dmr {
class BurstScreenshotsDialog: public DDialog {
public:
    BurstScreenshotsDialog(MpvProxy*);

public slots:
    int exec() override;
    void OnScreenshot(const QPixmap& frame);

private:
    MpvProxy *_mpv {nullptr};
    int _count {0};
    QGridLayout *_grid {nullptr};
};
}

#endif /* ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H */
